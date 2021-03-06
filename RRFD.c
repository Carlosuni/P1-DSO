#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

TCB* scheduler();
void activator();
void timer_interrupt(int sig);
void disk_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N]; 

/* Current running thread */
static TCB* running;
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* colas de prioridades */
struct queue* alta_prioridad;
struct queue* baja_prioridad;
struct queue* cola_de_espera;

/* Thread control block for the idle thread */
static TCB idle;
static void idle_function(){
  while(1);
}



/* Initialize the thread library */
void init_mythreadlib() {
  int i;  
  /* Create context for the idle thread */
  if(getcontext(&idle.run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }
  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;
  if(idle.run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  idle.run_env.uc_stack.ss_size = STACKSIZE;
  idle.run_env.uc_stack.ss_flags = 0;
  idle.ticks = QUANTUM_TICKS;
  makecontext(&idle.run_env, idle_function, 1); 

  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;
  if(getcontext(&t_state[0].run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }	
 	
  //inicializo las dos colas de prioridades
  alta_prioridad = queue_new ();
  baja_prioridad = queue_new ();
  cola_de_espera = queue_new ();
  
  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }
 
  t_state[0].tid = 0;
  running = &t_state[0];

  /* Initialize disk and clock interrupts */
  init_disk_interrupt();
  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */ 
int mythread_create (void (*fun_addr)(),int priority)
{
  int i;
  
  if (!init) { init_mythreadlib(); init=1;}
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;
  if (i == N) return(-1);
  if(getcontext(&t_state[i].run_env) == -1){
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  if(priority==HIGH_PRIORITY){
    enqueue(alta_prioridad, &t_state[i]);
  }
  if(priority==LOW_PRIORITY){
    enqueue(baja_prioridad, &t_state[i]); 
  }
  makecontext(&t_state[i].run_env, fun_addr, 1); 
  return i;
} /****** End my_thread_create() ******/

/* Read disk syscall */
int read_disk()
{
  if(data_in_page_cache()==0){
    //printf("*LOS DATOS SOLICITADOS YA ESTAN EN LA CACHE DE PAGINAs*\n");
    return 1;
  }
  int t_id = mythread_gettid(); 
  printf("*** THREAD %d READ FROM DISK\n", t_id);

  TCB* t = &t_state[t_id];
  t_state[t_id].state = WAITING;

  disable_interrupt();
  disable_disk_interrupt();
  enqueue(cola_de_espera, t);
  enable_interrupt();
  enable_disk_interrupt();

  TCB* next = scheduler();
  activator(next);

  return 1;
}

/*if the requested data is not already in the page cache*/
/*int data_in_page_cache(){
  return 1;
}
*/
/* Disk interrupt  */
void disk_interrupt(int sig)
{

  if(queue_empty(cola_de_espera) == 0){
    
    int t_id; 
    
    disable_interrupt();
    disable_disk_interrupt();
    TCB* t = dequeue(cola_de_espera);

    t_id = t->tid;
    t->state = INIT;
    printf("*** THREAD %d READY\n", t_id);

    if(t->priority >= HIGH_PRIORITY){
      enqueue(alta_prioridad, t);
    } else {
      enqueue(baja_prioridad, t);
    }

    activator(scheduler());
    enable_interrupt();
    enable_disk_interrupt();

  }
} 


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();	

  printf("*** THREAD %d FINISHED\n", tid);	
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp); 

  TCB* next = scheduler();
  activator(next);
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
  int tid = mythread_gettid();	
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) {
  int tid = mythread_gettid();	
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}


/* FIFO para alta prioridad, RR para baja*/
TCB* scheduler(){
//se rehece extrayendo las prioridades segun el ejercicio como lo indica
  if(queue_empty(alta_prioridad)== 0){ //la cola de prioridad alta no esta vacia 
    // coge el de prioridad uno
    disable_interrupt();
    TCB *p = dequeue(alta_prioridad);
    enable_interrupt();
	  return p;
  }
  //pasamos a los de priodidad baja cuando ya no quedan en la de alta prioridad
  if (queue_empty(baja_prioridad)==0){
    disable_interrupt();
    TCB* p = dequeue(baja_prioridad);
    enable_interrupt();
    return p;
  }
  if (running->state==INIT){
    return running;
  }
  printf("*** FINISH\n");
  exit(1);  
}


/* Timer interrupt  */
void timer_interrupt(int sig)
{
} 

/* Activator */
void activator(TCB* next){
  running->ticks= QUANTUM_TICKS;
  // se le ponen los ticks por defecto para optimizar el programa
  if (running == next){
    // se devuelve NULL si el que esta en ejecucion es el que se ejecutara a continuacion
    return;
  }
  TCB* anterior = running;
  running = next;
  current = running->tid;


  if (anterior->state == FREE){
    //solo se ejecuta cuando se produce un cambio de contexto
    printf("*** THREAD %d TERMINATED: SET CONTEXT OF %d\n", anterior->tid, running->tid);
    setcontext (&(next->run_env));
  }
  disable_interrupt();
  enqueue(baja_prioridad, anterior);
  enable_interrupt();

  /*****Just check if the thread was ejected or he just finish her quantum */
  /*If they was ejected*/
  if (running->priority == HIGH_PRIORITY){
    printf("*** THREAD %d PREEMTED: SET CONTEXT OF %d\n", anterior->tid, running->tid);
  } else {
    printf("*** SWAPCONTEXT FROM %d TO %d\n", anterior->tid,running->tid);
  }
  swapcontext(&anterior->run_env, &running->run_env);
}



