/*
  gcc -pthread scheduler.c -o scheduler; ./scheduler
*/

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/time.h>                 // gettimeofday
#include <pthread.h>                  // pthread, pthread_create
#include "fifo.c"

#define PIPE_PATH "./lab7-pipe.fifo"  // FIFO file path
#define INPUT_PATH "./input.txt"      // input of interpreter
#define BUF_SIZE 255                  // max size of string buffers
#define MAX_PROCS 50                  // max number of process this scheduler can handle

typedef enum {
	STATE_READY,
	STATE_WAITING,
} ProcState;

typedef struct {
  int id;
  char exec_path[BUF_SIZE];
  ProcState state;
} Process;

/***** global variables *****/

static int n_of_processes = 0;
static Process processes[MAX_PROCS];
static Fifo fifo_f1, fifo_f2, fifo_f3;

void *thread_pipe_main(void *arg) {
  int pipe_fd;
  char program_name[BUF_SIZE];

  printf("[pipe thread] started thread\n");
  while(1) {
    pipe_fd = open(PIPE_PATH, O_RDONLY);
    read(pipe_fd, program_name, BUF_SIZE);

    // create new process data
    Process new_proc;
    new_proc.id = n_of_processes;
    strcpy(new_proc.exec_path, program_name);
    new_proc.state = STATE_READY;

    // add process to the state of scheduler
    processes[n_of_processes] = new_proc;
    fifo_put(&fifo_f1, new_proc.id);
    n_of_processes++;

    // print new queue
    printf("[pipe thread] read from the pipe: '%s'\n", program_name);
    printf("[pipe thread] new queue = ");
    fifo_print(&fifo_f1);
    printf("\n");
  
    close(pipe_fd);
  }
  return NULL;
}

int main() {
  int fd1;
  pthread_t thread_pipe;

  // init global vars
  fifo_f1 = fifo_create();
  fifo_f2 = fifo_create();
  fifo_f3 = fifo_create();

  // start thread to handle interpreter inputs
  pthread_create(&thread_pipe, NULL, thread_pipe_main, NULL);

  while(1) {
    sleep(1);
    int handle = fifo_take(&fifo_f1);
    printf("took from queue: %d\n", handle);
  }

  return 0;
}
