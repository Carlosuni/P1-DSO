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
    while(queue_empty(alta_prioridad)==0){
      // coge el de prioridad uno, 
      TCB *p = dequeue(alta_prioridad);
      if((*p).state == INIT){
        current = (*p).tid;
	      return p;
      }
    }
  }else{ //pasamos a los de priodidad baja
    //printf("Planificador apartado 3.1.1\n");
    /*Disable interruptions to dequeue*/
    //disable_interrupt();
    /* Establish the next thread as the dequeued element from ReadyQueue */
    TCB* next = dequeue(baja_prioridad);
    /*Enable the interruptions*/
    //enable_interrupt();
    if((*next).state == INIT){
      current = (*next).tid;
      return next;
    }
  }
  printf("*** FINISH\n");
  exit(1); 
}


/* Timer interrupt  */
void timer_interrupt(int sig)
{
  if(running->priority == LOW_PRIORITY){
    running->ticks = (running->ticks) - 1;
    if (running->ticks <= 0){
      TCB *next = scheduler();
      activator(next);
    }
  }
} 

/* Activator */
void activator(TCB* next){
  //reliza el cambio de contexto y lo ejecuta 
  printf("*** THREAD %d READY\n", next->tid);
    /*Establish  thread ticks to default (QUANTUM_TICKS)*/
  running->ticks= QUANTUM_TICKS;

  /* Save the exiting thread progress*/
  TCB* previous_tcb = running;
  /* Establish the new dequeued thread (next) as the running one*/
  running = next;

  current = running->tid;

  if(running->priority== HIGH_PRIORITY){
    printf("*** THREAD %d TERMINATED: SET CONTEXT OF %d\n", previous_tcb->tid, running->tid);
    setcontext(&(running->run_env));
  }
  if(running->priority== LOW_PRIORITY){
    printf("*** SWAPCONTEXT FROM %d TO %d\n", current, next->tid);
    enqueue(baja_prioridad, running);
    swapcontext(&previous_tcb->run_env, &running->run_env);
    setcontext (&(running->run_env));
  }	
}



