/*
  gcc prog1.c -o prog1.exec; gcc prog2.c -o prog2.exec; gcc prog3.c -o prog3.exec; 

  prog1 does:
    burst of 10 T
*/

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>

#define PIPE_IO_START "./io-start.pipe"   // named pipe to start IO operation
#define PIPE_IO_END   "./io-end.pipe"     // named pipe to end IO operation

#define QUANTUM_BASE 2  // in seconds

// stoptime = sum of all time this process has been stopped
// for one stop: time_stop = (tv4-tv3)
struct timeval tv3, tv4;
double stoptime = 0;

// returns the diff of two times, in microsseconds
double diff(struct timeval *start, struct timeval *end) {
  return (double) (end->tv_usec - start->tv_usec) +
         (double) 1000000*(end->tv_sec - start->tv_sec);
}

// SIGUSR1 is our SIGSTOP
void sigusr1_handler() {
  printf("[pid %d] received a SIGUSR1 -> STOP\n", getpid());
  gettimeofday(&tv3, NULL);
  pause();
}

// SIGUSR2 is our SIGCONT
void sigusr2_handler() {
  printf("[pid %d] received a SIGUSR2 -> RUN\n", getpid());
  gettimeofday(&tv4, NULL);
  stoptime += diff(&tv3, &tv4);
}

void run_burst(int burst_size) {
  // runtime_ms = (tv2-tv1) - stoptime
  struct timeval tv1, tv2;
  double runtime_ms = 0;
  int runtime_round = 0;
  int max_time = 1000000*QUANTUM_BASE*burst_size; // in ms

  // reset time counters
  stoptime = 0;
  gettimeofday(&tv1, NULL);

  printf("[pid %d] started burst\n", getpid());
  do {
    gettimeofday(&tv2, NULL);
    runtime_ms = diff(&tv1, &tv2) - stoptime;

    // log each second
    if( ((int) runtime_ms/1000000) > runtime_round ) {
      runtime_round++;
      printf("[pid %d] running burst for %d seconds (runtime_ms = %f)\n",
              getpid(), runtime_round, runtime_ms);
    }
  } while(runtime_ms < max_time);
  printf("[pid %d] finished burst\n", getpid());
}

void run_IO(int io_time) {
  // we don't consider "stoptime" in the IO
  struct timeval tv1, tv2;
  double runtime_ms = 0;
  int runtime_round = 0;
  int pipe_fp;
  int max_time = 1000000*QUANTUM_BASE*io_time; // in ms
  int pid = getpid();

  printf("[pid %d] started IO\n", getpid());
  gettimeofday(&tv1, NULL);

  // warn scheduler about IO start
  pipe_fp = open(PIPE_IO_START, O_WRONLY);
  write(pipe_fp, &pid, sizeof(pid));
  close(pipe_fp);

  do {
    gettimeofday(&tv2, NULL);
    runtime_ms = diff(&tv1, &tv2);

    // log each second
    if( ((int) runtime_ms/1000000) > runtime_round ) {
      runtime_round++;
      printf("[pid %d] running IO for %d seconds (runtime_ms = %f)\n",
              getpid(), runtime_round, runtime_ms);
    }
  } while(runtime_ms < max_time);
  printf("[pid %d] finished IO\n", getpid());

  // warn scheduler about IO end
  pipe_fp = open(PIPE_IO_END, O_WRONLY);
  write(pipe_fp, &pid, sizeof(pid));
  close(pipe_fp);
}

int main() {
  signal(SIGUSR1, sigusr1_handler);
  signal(SIGUSR2, sigusr2_handler);

  // wait for a SIGUSR2 signal to start
  kill(getpid(), SIGUSR1);

  run_burst(10);
  // run_IO(2);
}
