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
/* Waiting queue for the process that calls read_network */
queue_ptr waiting_queue;

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
  waiting_queue = queue_new();

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

  disable_interrupt();
  /* Add process to the correct queue */
  if (priority == LOW_PRIORITY) {
    enqueue(ready_queue, &t_state[i]);
  } else {
    enqueue(high_priority_queue, &t_state[i]);
    if (running->priority == LOW_PRIORITY) {
      trigger_switch(); 
    }
  }
  enable_interrupt();
  return i;
} /****** End my_thread_create() ******/


/* Read network syscall */
int read_network()
{
	TCB * next;
	
	printf("*** THREAD %i READ FROM NETWORK\n", current);
	
	disable_interrupt();
	enqueue(waiting_queue,running);
	next = scheduler();
	enable_interrupt();
	  
	running->state = WAITING;
	getcontext(&(running->run_env));
	
	if (running->state == WAITING) {
		running->ticks=0;
		activator(next);
	}
	
	return 1;
}

/* Network interrupt  */
void network_interrupt(int sig)
{
	TCB * tmp_tcb;
	
	disable_interrupt();
	/* Not a while because we're only dequeuing the 1st thread */
	if (!queue_empty(waiting_queue)) {
		tmp_tcb = dequeue(waiting_queue);
		tmp_tcb->state = INIT;
		enqueue(tmp_tcb->priority == HIGH_PRIORITY ? high_priority_queue : ready_queue, tmp_tcb);
	}
	enable_interrupt();
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
		if (queue_empty(waiting_queue)) {
			printf("*** THREAD %d FINISHED\n", current);
			printf("mythread_free: No thread in the system\nExiting...\n");	
			exit(1); 
		} else return &idle;
  }
  TCB * next;
  if (queue_empty(high_priority_queue)) {
    next = dequeue(ready_queue);
  } else {
    next = dequeue(high_priority_queue);
  }
  
  return next;

}


/* Not in the case of a read_network */
/* Be careful:
 * Assuming interrupts are disabled */
void trigger_switch() {
   //   Enqueue this process in the correct queue 
    getcontext(&running->run_env);
    // If it's time
    if (running->priority == LOW_PRIORITY && (running->ticks >= QUANTUM_TICKS || !queue_empty(high_priority_queue))) {
		
      enqueue(ready_queue, running);

      running->state = IDLE;
      running->ticks = 0;
	  enable_interrupt();
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
	TCB * next_pr;
	running->ticks++;
	disable_interrupt();  
	if (running->tid==-1) {
		next_pr = scheduler();
		if (next_pr->tid!=-1) {
			enable_interrupt();
			activator(next_pr);
		}
	}
	else if (running->ticks >= QUANTUM_TICKS) trigger_switch();
	
	enable_interrupt();
}

/* Activator */
void activator(TCB* next){
  running = next; 
  current = next->tid;
        
  setcontext (&(next->run_env));
  printf("mythread_free: After setcontext, should never get here!!...\n");	
}



