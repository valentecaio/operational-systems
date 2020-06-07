/*
  gcc interpreter.c -o interpreter; ./interpreter
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PIPE_INPUT "./new-process.pipe" // named pipe for creating new processes
#define INPUT_PATH "./input.txt"        // input of interpreter
#define BUF_SIZE 255                    // max size of string buffers

int str_starts_with(const char *a, const char *b) {
   return (strncmp(a, b, strlen(b)) == 0) ? 1 : 0;
}

int main() {
  int pipe_fd;
  char program_name[BUF_SIZE];
  char line_buffer[BUF_SIZE];
  FILE* input_fp;

  // create named pipe (FIFO)
  mkfifo(PIPE_INPUT, 0666);

  // handle input file line by line
  input_fp = fopen(INPUT_PATH, "r");
  while(fgets(line_buffer, BUF_SIZE, input_fp)) {
    // we don't need the \n in the end
    strtok(line_buffer, "\n");
    printf("read: '%s' from the file\n", line_buffer);

    // validate line syntax
    if(!str_starts_with(line_buffer, "exec ")) {
      printf("SKIPPED line '%s' -> Lines must start with 'exec '.\n", line_buffer);
      continue;
    }
    if(strlen(line_buffer) < 6) {
      printf("SKIPPED line '%s' -> Program name is empty.\n", line_buffer);
      continue;
    }
    strcpy(program_name, &line_buffer[5]);
    if(access(program_name, F_OK) == -1) {
      printf("SKIPPED line '%s' -> File '%s' does not exist.\n", line_buffer, program_name);
      continue;
    }

    // send each valid program to scheduler
    pipe_fd = open(PIPE_INPUT, O_WRONLY);
    write(pipe_fd, program_name, strlen(program_name)+1);
    close(pipe_fd);
    printf("wrote '%s' to the pipe\n", program_name);
  }

  fclose(input_fp);
  return 0;
}
