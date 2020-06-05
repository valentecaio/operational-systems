/*
  gcc scheduler.c fifo.c -pthread -o scheduler; ./scheduler
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>     // gettimeofday
#include <pthread.h>      // pthread, pthread_create
#include <sys/time.h>     // gettimeofday
#include <signal.h>       // signal, kill

#include "fifo.h"

#define PIPE_INPUT    "./new-process.pipe"  // named pipe for incoming new processes
#define PIPE_IO_START "./io-start.pipe"     // named pipe to start IO operation
#define PIPE_IO_END   "./io-end.pipe"       // named pipe to end IO operation

#define BUF_SIZE 255    // max size of string buffers
#define MAX_PROCS 50    // max number of process this scheduler can handle
#define QUANTUM_BASE 2  // in seconds

typedef enum {
	STATE_READY,
	STATE_WAITING,
} ProcState;

typedef struct {
  int fid;              // "FIFO id" = id of this process in the fifo queues
  int pid;              // unix pid
  int priority;         // 1 for fifo_f1, 2 for fifo_f2, 4 for fifo_f3
  char prog[BUF_SIZE];  // path of the file containing the code this process may run
  ProcState state;
} Process;


/***** scheduler state *****/

static int n_of_processes = 0;
Process processes[MAX_PROCS];           // the index here is the p.fid
static Fifo fifo_f1, fifo_f2, fifo_f3;



void print_proc(Process *p) {
  printf("  {fid: %d, pid: %d, prog: %s, priority: %d, state: %s},\n",
        p->fid, p->pid, p->prog, p->priority,
        (p->state==STATE_READY) ? "READY" : "WAITING");
}

// not thread-safe, used in tests
void print_processes() {
  printf("Processes = [\n");
  for(int i=0; i < n_of_processes; i++) {
    print_proc(&processes[i]);
  }
  printf("]\n");
}



/***** pipe handlers *****/

// this thread handles interpreter input (create new processes)
void *t_pipe_input_main(void *arg) {
  int pipe_fd, pid, next_fid;
  char program_name[BUF_SIZE];

  printf("[thread pipe input] started thread\n");
  while(1) {
    pipe_fd = open(PIPE_INPUT, O_RDONLY);
    read(pipe_fd, program_name, BUF_SIZE);

    next_fid = n_of_processes;
    n_of_processes++;

    // create child process for this program
    if((pid=fork()) == 0) {
      char *args[] = {program_name, NULL};
      printf("[child {%d, '%s'}] created. Launching exec()\n", next_fid, program_name);
      execv(args[0], args);
      return NULL;
    }

    /*** only the parent (scheduler) gets here ***/

    // create new process data
    Process new_proc;
    new_proc.pid = pid;
    new_proc.fid = next_fid;
    new_proc.priority = 1;
    new_proc.state = STATE_READY;
    strcpy(new_proc.prog, program_name);

    // add process data to the state of scheduler
    processes[next_fid] = new_proc;
    fifo_put(&fifo_f1, new_proc.fid);

    // print new state
    printf("[thread pipe input] new queue = ");
    fifo_print(&fifo_f1);
    printf("\n");

    print_processes();

    close(pipe_fd);
  }
  return NULL;
}


int main() {
  int fid, quantum;
  pthread_t t_pipe_input;
  struct timeval tv1, tv2;
  double runtime;
  Process *p;

  // init global vars
  fifo_f1 = fifo_create();
  fifo_f2 = fifo_create();
  fifo_f3 = fifo_create();

  // create named pipes (FIFO)
  mkfifo(PIPE_INPUT, 0666);
  mkfifo(PIPE_IO_START, 0666);
  mkfifo(PIPE_IO_END, 0666);

  // start thread for handling input from interpreter
  pthread_create(&t_pipe_input, NULL, t_pipe_input_main, NULL);

  while(1) {
    // get next process to run
    fid = fifo_take(&fifo_f1);
    printf("[main] took from fifo_f1: %d\n", fid);
    if(fid < 0) {
      // f1 empty: try f2
      fid = fifo_take(&fifo_f2);
      printf("[main] took from fifo_f2: %d\n", fid);
      if(fid < 0) {
        // f2 empty: try f3
        fid = fifo_take(&fifo_f3);
        printf("[main] took from fifo_f3: %d\n", fid);
        if(fid < 0) {
          // all queues are empty: wait and retry
          sleep(1);
          continue;
        }
      }
    }
    p = &processes[fid];
    printf("[main] next process to run:");
    print_proc(p);

    // run process for quantum time
    quantum = 1000000 * QUANTUM_BASE * p->priority;
    kill(p->pid, SIGUSR2);
    gettimeofday(&tv1, NULL);
    do {
      gettimeofday(&tv2, NULL);
      runtime = (double) (tv2.tv_usec - tv1.tv_usec) + (double) 1000000*(tv2.tv_sec - tv1.tv_sec);
    } while(runtime < quantum);

    // stop process
    printf("[main] %d achieved the quantum. Stopping it.\n", fid);
    kill(p->pid, SIGUSR1);

    // reduce priority and put process in a lower level queue
    if(p->priority == 1) {
      p->priority = 2;
      fifo_put(&fifo_f2, fid);
    } else {
      p->priority = 4;
      fifo_put(&fifo_f3, fid);
    }
  }

  return 0;
}
