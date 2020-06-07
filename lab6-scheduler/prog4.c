/*
  gcc prog1.c -o prog1; gcc prog2.c -o prog2; gcc prog3.c -o prog3; gcc prog4.c -o prog4;

  prog4 does: 5,3,4 = 5 UT burst, 3 UT IO, 3 UT burst, 3 UT IO, 4 UT burst
*/

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#define UT 2  // in seconds

// stoptime = sum of all time this process has been stopped (ordered by the scheduler)
struct timeval time_stopped_at;
double stoptime = 0;

int mypid;

// returns the diff of two times, in microsseconds
double diff(struct timeval *end, struct timeval *start) {
  return (double) (end->tv_usec - start->tv_usec) +
         (double) 1000000*(end->tv_sec - start->tv_sec);
}

// SIGUSR1 is our SIGSTOP
void sigusr1_handler() {
  printf("[pid %d] received a SIGUSR1 -> STOP\n", mypid);
  gettimeofday(&time_stopped_at, NULL);
  pause();
}

// SIGUSR2 is our SIGCONT
void sigusr2_handler() {
  struct timeval time_now;
  printf("[pid %d] received a SIGUSR2 -> RUN\n", mypid);
  gettimeofday(&time_now, NULL);
  stoptime += diff(&time_now, &time_stopped_at);
}

void run_burst(int burst_size) {
  // runtime_ms = (time_now-time_burst_start) - stoptime
  struct timeval time_burst_start, time_now;
  double runtime_ms = 0;
  int runtime_ut = 0;
  int max_time = 1000000*UT*burst_size; // in ms

  // reset time counters
  stoptime = 0;
  gettimeofday(&time_burst_start, NULL);

  printf("[pid %d] started burst\n", mypid);
  do {
    gettimeofday(&time_now, NULL);
    runtime_ms = diff(&time_now, &time_burst_start) - stoptime;

    // log each UT
    if( ((int) runtime_ms/1000000) > (runtime_ut*UT) ) {
      runtime_ut++;
      printf("[pid %d] completed %d UT of BURST.\n",
              mypid, runtime_ut);
    }
  } while(runtime_ms < max_time);
  printf("[pid %d] finished burst\n", mypid);
}

void run_IO(int io_time) {
  // we don't consider "stoptime" in the IO
  struct timeval time_io_start, time_now;
  double runtime_ms = 0;
  int runtime_ut = 0;
  int pipe_fp;
  int max_time = 1000000*UT*io_time; // in ms

  // warn scheduler about IO start
  kill(getppid(), SIGUSR1);

  printf("[pid %d] started IO\n", mypid);
  gettimeofday(&time_io_start, NULL);

  do {
    gettimeofday(&time_now, NULL);
    runtime_ms = diff(&time_now, &time_io_start);

    // log each UT
    if( ((int) runtime_ms/1000000) > (runtime_ut*UT) ) {
      runtime_ut++;
      printf("[pid %d] completed %d UT of IO OPERATION.\n",
              mypid, runtime_ut);
    }
  } while(runtime_ms < max_time);
  printf("[pid %d] finished IO\n", mypid);

  // warn scheduler about IO end and wait to be re-scheduled
  kill(getppid(), SIGUSR2);
  pause();
}

int main() {
  mypid = getpid();

  signal(SIGUSR1, sigusr1_handler);
  signal(SIGUSR2, sigusr2_handler);

  // wait for a SIGUSR2 signal to start
  kill(mypid, SIGUSR1);

  run_burst(5);
  run_IO(3);
  run_burst(3);
  run_IO(3);
  run_burst(4);
}
