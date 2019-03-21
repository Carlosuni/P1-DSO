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

/* actual en_ejecucion thread */
static TCB* en_ejecucion;
static int actual = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* colas de prioridades */
struct queue* alta_prioridad;
struct queue* baja_prioridad;

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

  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }
 
  t_state[0].tid = 0;
  en_ejecucion = &t_state[0];

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
  makecontext(&t_state[i].run_env, fun_addr, 1); 

  TCB *actual = &t_state[i];

  if(priority==HIGH_PRIORITY){    
    if (queue_empty(alta_prioridad) == 1 && en_ejecucion->priority == LOW_PRIORITY){ /* o igual a 0¿?*/
      activator(actual);
    }else{
      disable_interrupt();
      enqueue(alta_prioridad, &t_state[i]);
      enable_interrupt();
    }
  }
  if(priority==LOW_PRIORITY){
    disable_interrupt();
    enqueue(baja_prioridad, &t_state[i]); 
    enable_interrupt();
  }
  return i;
} /****** End my_thread_create() ******/

/* Read disk syscall */
int read_disk()
{
   return 1;
}

/* Disk interrupt  */
void disk_interrupt(int sig)
{
} 


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();	
  printf("*** THREAD %d FINISHED\n", tid);
  //preparamos el thread a ejecutar
  TCB* siguiente = scheduler();	
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp);
  //lanzamos la ejecucion 
  activator(siguiente);
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


/* Get the actual thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return actual;
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
  //}else{ //pasamos a los de priodidad baja cuando ya no quedan en la de alta prioridad
  }

  if (queue_empty(baja_prioridad)==0){
    disable_interrupt();
    TCB* p = dequeue(baja_prioridad);
    enable_interrupt();
    return p;
  }
  if (en_ejecucion->state==INIT){
    return en_ejecucion;
  }
  
  printf("*** FINISH\n");
  exit(1); 
}


/* Timer interrupt  */
void timer_interrupt(int sig)
{
  if (en_ejecucion->priority == HIGH_PRIORITY){
    return;
  }
  en_ejecucion->ticks = (en_ejecucion->ticks) - 1;
  if (en_ejecucion->ticks <= 0){
    TCB *siguiente = scheduler();
    activator(siguiente);
  }
} 

/* Activator */
void activator(TCB* siguiente){
  en_ejecucion->ticks= QUANTUM_TICKS;
  // se le ponen los ticks por defecto para optimizar el programa
  if (en_ejecucion == siguiente){
    // se devuelve NULL si el que esta en ejecucion es el que se ejecutara a continuacion
    return;
  }
  TCB* anterior = en_ejecucion;
  en_ejecucion = siguiente;
  actual = en_ejecucion->tid;


  if (anterior->state == FREE){
    //solo se ejecuta cuando se produce un cambio de contexto
    printf("*** THREAD %d TERMINATED: SET CONTEXT OF %d\n", anterior->tid, en_ejecucion->tid);
    setcontext (&(siguiente->run_env));
  }
  disable_interrupt();
  enqueue(baja_prioridad, anterior);
  enable_interrupt();

  /*****Just check if the thread was ejected or he just finish her quantum */
  /*If they was ejected*/
  if (en_ejecucion->priority == HIGH_PRIORITY){
    printf("*** THREAD %d PREEMTED: SET CONTEXT OF %d\n", anterior->tid, en_ejecucion->tid);
  } else {
    printf("*** SWAPCONTEXT FROM %d TO %d\n", anterior->tid,en_ejecucion->tid);
  }
  swapcontext(&anterior->run_env, &en_ejecucion->run_env);
}



