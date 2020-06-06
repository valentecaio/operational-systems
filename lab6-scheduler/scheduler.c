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
#include <sys/wait.h>     // WNOHANG

#include "fifo.h"

#define PIPE_INPUT    "./new-process.pipe"  // named pipe for incoming new processes
#define PIPE_IO_START "./io-start.pipe"     // named pipe to start IO operation
#define PIPE_IO_END   "./io-end.pipe"       // named pipe to end IO operation

#define BUF_SIZE 255    // max size of string buffers
#define MAX_PROCS 50    // max number of process this scheduler can handle
#define QUANTUM_BASE 2  // in seconds

typedef struct {
  int fid;              // "FIFO id" = id of this process in the fifo queues
  int pid;              // unix pid
  int priority;         // 1 for fifo_f1, 2 for fifo_f2, 4 for fifo_f3
  char prog[BUF_SIZE];  // path of the file containing the code this process may run
} Process;


/***** scheduler state *****/

static int n_of_processes = 0;
Process processes[MAX_PROCS];           // the index here is the p.fid
Process *running_proc;
int flag_io;
static Fifo fifo_f1, fifo_f2, fifo_f3;



void print_proc(Process *p) {
  printf("  {fid: %d, pid: %d, prog: %s, priority: %d},\n",
        p->fid, p->pid, p->prog, p->priority);
}

// not thread-safe, used in tests
void print_processes() {
  printf("Processes = [\n");
  for(int i=0; i < n_of_processes; i++) {
    print_proc(&processes[i]);
  }
  printf("]\n");
}

// not thread safe
void print_fifos() {
  printf("FIFO F1 = ");
  fifo_print(&fifo_f1);
  printf("\nFIFO F2 = ");
  fifo_print(&fifo_f2);
  printf("\nFIFO F3 = ");
  fifo_print(&fifo_f3);
  printf("\n");
}

// get the higher priority process of all 3 queues
int dequeue() {
  // try to get from fifo f1
  int fid = fifo_take(&fifo_f1);
  if(fid < 0) {
    // f1 empty: try f2
    fid = fifo_take(&fifo_f2);
    if(fid < 0) {
      // f2 empty: try f3
      fid = fifo_take(&fifo_f3);
    }
  }
  return fid;
}

// put process in a queue, according to its current priority
int enqueue(Process *p) {
  if(p->priority == 1) {
    fifo_put(&fifo_f1, p->fid);
  } else if(p->priority == 2) {
    fifo_put(&fifo_f2, p->fid);
  } else {
    fifo_put(&fifo_f3, p->fid);
  }
}



/***** signal handlers *****/

// SIGUSR1 is used to signal an IO start
// get sender pid and block this process
void sigusr1_handler(int signo, siginfo_t *si, void *data) {
  int sender = (unsigned long)si->si_pid;
  printf("[SCHEDULER] [SIGUSR1] received a SIGUSR1 from %d\n", sender);

  // block running process
  flag_io = 1;
}

// SIGUSR2 is used to signal an IO end
// get sender pid and unblock this process
void sigusr2_handler(int signo, siginfo_t *si, void *data) {
  int pid = (unsigned long)si->si_pid;
  int fid;
  printf("[SCHEDULER] [SIGUSR2] received a SIGUSR2 from %d\n", pid);

  // find the fid of the sender
  for(int i=0; i < n_of_processes; i++) {
    if(processes[i].pid == pid) {
      fid = processes[i].fid;
      break;
    }
  }

  // process unblocked -> add it to the right queue
  printf("[SCHEDULER] [SIGUSR2] process unblocked:");
  print_proc(&processes[fid]);
  enqueue(&processes[fid]);
}



/***** pipe handlers *****/

// this thread handles interpreter input (create new processes)
void *t_pipe_input_main(void *arg) {
  int pipe_fd, pid, next_fid;
  char program_name[BUF_SIZE];

  printf("[PIPE THREAD] started thread\n");
  while(1) {
    pipe_fd = open(PIPE_INPUT, O_RDONLY);
    read(pipe_fd, program_name, BUF_SIZE);

    next_fid = n_of_processes;
    n_of_processes++;

    // create child process for this program
    if((pid=fork()) == 0) {
      char *args[] = {program_name, NULL};
      execv(args[0], args);
      return NULL;
    }

    /*** only the parent (scheduler) gets here ***/

    // create new process data
    Process new_proc;
    new_proc.pid = pid;
    new_proc.fid = next_fid;
    new_proc.priority = 1;
    strcpy(new_proc.prog, program_name);

    // add process data to the state of scheduler
    processes[next_fid] = new_proc;
    fifo_put(&fifo_f1, next_fid);

    printf("[PIPE THREAD] Created new process:");
    print_proc(&processes[next_fid]);

    // print new state
    print_fifos();
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

  printf("[SCHEDULER] scheduler pid: %d\n", getpid());

  // init global vars
  fifo_f1 = fifo_create();
  fifo_f2 = fifo_create();
  fifo_f3 = fifo_create();

  // create named pipes (FIFO)
  mkfifo(PIPE_INPUT, 0666);
  mkfifo(PIPE_IO_START, 0666);
  mkfifo(PIPE_IO_END, 0666);

  // set handler for SIGUSR1
  struct sigaction sa1, sa2;
  memset(&sa1, 0, sizeof(sa1));
  sa1.sa_flags = SA_SIGINFO;
  sa1.sa_sigaction = sigusr1_handler;
  sigaction(SIGUSR1, &sa1, 0);

  // set handler for SIGUSR2
  memset(&sa2, 0, sizeof(sa2));
  sa2.sa_flags = SA_SIGINFO;
  sa2.sa_sigaction = sigusr2_handler;
  sigaction(SIGUSR2, &sa2, 0);

  // start thread to handle input from interpreter
  pthread_create(&t_pipe_input, NULL, t_pipe_input_main, NULL);

  while(1) {
    sleep(1);
    printf("\n");
    print_fifos();
    printf("\n");

    // get next process to run
    fid = dequeue();
    if(fid < 0) {
      // all queues are empty: wait and retry
      sleep(1);
      continue;
    }
    running_proc = &processes[fid];
    printf("[SCHEDULER] next process to run:");
    print_proc(running_proc);

    // reset flags before running
    flag_io = 0;

    // run process for quantum time, or until it stops for IO
    quantum = 1000000 * QUANTUM_BASE * running_proc->priority;
    kill(running_proc->pid, SIGUSR2);
    gettimeofday(&tv1, NULL);
    do {
      gettimeofday(&tv2, NULL);
      runtime = (double) (tv2.tv_usec - tv1.tv_usec) + (double) 1000000*(tv2.tv_sec - tv1.tv_sec);
    } while((runtime < quantum) && !flag_io);

    // if this process ended, we will just ignore it from now on
    if(waitpid(running_proc->pid, NULL, WNOHANG) == running_proc->pid) {
      printf("[SCHEDULER] %d ended. Removing it from queues.\n", running_proc->pid);
      continue;
    }

    if(flag_io){
      printf("[SCHEDULER] %d is running an IO operation. CPU is free.\n", running_proc->pid);
      // increase priority but keep process out of any queue
      // it will join a queue when the IO finishes (SIGUSR2)
      if(running_proc->priority == 4) {
        running_proc->priority = 2;
      } else {
        running_proc->priority = 1;
      }
    } else {
      // stop process
      printf("[SCHEDULER] %d achieved the quantum. Stopping it.\n", running_proc->pid);
      kill(running_proc->pid, SIGUSR1);

      // reduce priority and put process in a lower level queue
      if(running_proc->priority == 1) {
        running_proc->priority = 2;
      } else {
        running_proc->priority = 4;
      }
      enqueue(running_proc);
    }
  }

  return 0;
}
