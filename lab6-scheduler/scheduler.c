/*
  gcc scheduler.c fifo.c -pthread -o scheduler; ./scheduler
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>        // open, close
#include <pthread.h>      // pthread, pthread_create
#include <signal.h>       // sigaction, kill
#include <sys/stat.h>     // mkfifo
#include <sys/time.h>     // gettimeofday
#include <sys/wait.h>     // WNOHANG

#include "fifo.h"

#define PIPE_INPUT "./input.pipe" // named pipe for incoming new processes

#define BUF_SIZE 255    // max size of string buffers
#define MAX_PROCS 64    // max number of process this scheduler can handle
#define UT 2            // in seconds

typedef struct {
  int fid;              // "FIFO id"  = id of this process in this scheduler
  int pid;              // "unix pid" = id of this process in the OS
  int priority;         // 1 for fifo_f1, 2 for fifo_f2, 4 for fifo_f3
  char prog[BUF_SIZE];  // path of the file containing the code this process may run
} Process;



/***** scheduler state *****/

Fifo fifo_f1, fifo_f2, fifo_f3; // integer fifos
Process processes[MAX_PROCS];   // the index here is the p.fid
int n_of_processes = 0;         // the length of 'processes' list

int flag_io;  // flag "the running process started an IO operation"
int flag_end; // flag "the running process ended"



/***** auxiliary functions *****/

void print_proc(Process *p) {
  printf("  {fid: %d, pid: %d, prog: %s, priority: %d},\n",
        p->fid, p->pid, p->prog, p->priority);
}

// not thread-safe
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
// returns -1 if all queues are empty
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

  // SIGCHLD is used to signal that a process ended
  void sigchld_handler(int signo, siginfo_t *si, void *data) {
    int sender = (unsigned long)si->si_pid;
    printf("[SCHEDULER] [SIGCHLD] received a SIGCHLD from %d\n", sender);

    // block running process forever
    flag_end = 1;
  }


/***** pipe handlers *****/

// this thread handles interpreter input (create new processes)
void *t_pipe_input_main(void *arg) {
  int pipe_fd, pid, next_fid;
  char program_name[BUF_SIZE];

  printf("[PIPE THREAD] started thread\n");

  // create named pipe (FIFO)
  mkfifo(PIPE_INPUT, 0666);

  while(1) {
    pipe_fd = open(PIPE_INPUT, O_RDONLY);
    read(pipe_fd, program_name, BUF_SIZE);
    close(pipe_fd);

    // ignore repeated programs
    int flag_repeated = 0;
    for(int i=0; i<n_of_processes; i++) {
      if(strcmp(processes[i].prog, program_name) == 0) {
        flag_repeated = 1;
        break;
      }
    }
    if (flag_repeated) continue;

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

    // print new state
    printf("[PIPE THREAD] Created new process:");
    print_proc(&processes[next_fid]);

    print_processes();
    // print_fifos();
  }
  return NULL;
}


int main() {
  int fid, quantum;
  pthread_t t_pipe_input;
  struct sigaction sa1, sa2, sa3;
  struct timeval tv1, tv2;
  double runtime;
  Process *p;

  printf("[SCHEDULER] started scheduler with pid %d\n", getpid());

  // init queues
  fifo_f1 = fifo_create();
  fifo_f2 = fifo_create();
  fifo_f3 = fifo_create();

  // set handler for SIGUSR1 -> "IO start signal"
  memset(&sa1, 0, sizeof(sa1));
  sa1.sa_flags = SA_SIGINFO;
  sa1.sa_sigaction = sigusr1_handler;
  sigaction(SIGUSR1, &sa1, 0);

  // set handler for SIGUSR2 -> "IO end signal"
  memset(&sa2, 0, sizeof(sa2));
  sa2.sa_flags = SA_SIGINFO;
  sa2.sa_sigaction = sigusr2_handler;
  sigaction(SIGUSR2, &sa2, 0);

  // set handler for SIGCHLD -> "process finished signal"
  memset(&sa3, 0, sizeof(sa2));
  sa3.sa_flags = SA_SIGINFO;
  sa3.sa_sigaction = sigchld_handler;
  sigaction(SIGCHLD, &sa3, 0);

  // start thread to handle input from interpreter
  pthread_create(&t_pipe_input, NULL, t_pipe_input_main, NULL);

  while(1) {
    // we don't actually need this sleep(1) but it helps seeing the logs
    // without it, logs of the scheduler are mixed with logs of child procs
    sleep(1);

    // print all queues every time we will choose a process to run
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
    p = &processes[fid];
    printf("[SCHEDULER] next process to run:");
    print_proc(p);

    // reset flags before running
    flag_io = 0;
    flag_end = 0;

    // run process for quantum time, or until it stops for IO or ends
    quantum = 1000000 * UT * p->priority;
    kill(p->pid, SIGUSR2);
    gettimeofday(&tv1, NULL);
    do {
      gettimeofday(&tv2, NULL);
      runtime = (double) (tv2.tv_usec - tv1.tv_usec) + (double) 1000000*(tv2.tv_sec - tv1.tv_sec);
    } while((runtime < quantum) && !flag_io && !flag_end);

    if(flag_end) {
      printf("[SCHEDULER] %d ended. Removing it from queues.\n", p->pid);
      // if this process ended, we will just ignore it from now on
      continue;
    }

    if(flag_io){
      printf("[SCHEDULER] %d is running an IO operation. CPU is free.\n", p->pid);

      // we only increase priority if the process left at least half UT of quantum unused
      // otherwise, we consider that it used "exactly" the whole quantum, without blowing it
      if( ((int)runtime) < (quantum-1000000) ) {
        // increase priority but keep process out of any queue
        // it will join a queue when the IO finishes (SIGUSR2)
        if(p->priority == 4) {
          p->priority = 2;
        } else {
          p->priority = 1;
        }
      }
    } else {
      printf("[SCHEDULER] %d achieved the quantum. Stopping it.\n", p->pid);
      // stop process
      kill(p->pid, SIGUSR1);

      // reduce priority and put process in a lower level queue
      if(p->priority == 1) {
        p->priority = 2;
      } else {
        p->priority = 4;
      }
      enqueue(p);
    }
  }

  return 0;
}
