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
void network_interrupt(int sig);
void trigger_switch(void);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N]; 

/* Current running thread */
static TCB* running;
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* Thread control block for the idle thread */
static TCB idle;
static void idle_function(){
  while(1);
}

/* Queue for Round robin and FIFO */
typedef struct queue *queue_ptr;
queue_ptr ready_queue;
queue_ptr high_priority_queue;

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

  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }
 
  t_state[0].tid = 0;
  running = &t_state[0];

  /* Initialize queues */

  /*
  * Ready queue: list of TCB *  in order that they will execute
  */
  ready_queue = queue_new();
  high_priority_queue = queue_new();

  /* Initialize network and clock interrupts */
  init_network_interrupt();
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

  /* Add process to the correct queue */
  if (priority == LOW_PRIORITY) {
    enqueue(ready_queue, &t_state[i]);
  } else {
    enqueue(high_priority_queue, &t_state[i]);
    if (running->priority == LOW_PRIORITY) {
      trigger_switch(); 
    }
  }
  return i;
} /****** End my_thread_create() ******/

/* Read network syscall */
int read_network()
{
   return 1;
}

/* Network interrupt  */
void network_interrupt(int sig)
{
} 


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();	

  /* Do not add the thread back into the queue */    

  TCB* next = scheduler();
  printf("*** THREAD %d FINISHED: SET CONTEXT OF %d\n", tid, next->tid);

  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp); 

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


/* Round robin policy */
TCB* scheduler(){

  if (queue_empty(high_priority_queue) && queue_empty(ready_queue)) {
    printf("*** THREAD %d FINISHED\n", current);
    printf("FINISH\n");	
    exit(1); 
  }
  TCB * next;
  if (queue_empty(high_priority_queue)) {
    next = dequeue(ready_queue);
  } else {
    next = dequeue(high_priority_queue);
  }
  
  return next;

}

void trigger_switch() {
   //   Enqueue this process in the correct queue 
    getcontext(&running->run_env);
    // If it's time
    if (running->priority == LOW_PRIORITY && (running->ticks >= QUANTUM_TICKS || !queue_empty(high_priority_queue))) {
            
      if (running->priority == LOW_PRIORITY) {
        enqueue(ready_queue, running);
      } else {
        enqueue(high_priority_queue, running);
      }
      running->state = IDLE;
      running->ticks = 0;

      //   Call Scheduler
      TCB* next = scheduler();
      if (next->tid != running->tid) {
        if (running->priority == LOW_PRIORITY && next->priority == HIGH_PRIORITY) {
            printf("*** THREAD %d PREEMPTED: SET CONTEXT OF %d\n", running->tid, next->tid);
        } else {
            printf("*** SWAPCONTEXT FROM %d to %d\n", running->tid, next->tid);
        }
        activator(next);
      }
    }
}


/* Timer interrupt  */
void timer_interrupt(int sig)
{
  running->ticks++;
  if (running->ticks >= QUANTUM_TICKS) {
    trigger_switch();
  }
}

/* Activator */
void activator(TCB* next){
  running = next; 
  current = next->tid;
        
  setcontext (&(next->run_env));
  printf("mythread_free: After setcontext, should never get here!!...\n");	
}



