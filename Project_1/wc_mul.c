#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#define MAX_PROC 100
#define MAX_FORK 1000

typedef struct count_t {
  int linecount;
  int wordcount;
  int charcount;
} count_t;

int CRASH = 0;

count_t word_count(FILE* fp, long offset, long size) {
  char ch;
  long rbytes = 0;
  count_t count = {0, 0, 0};

  printf("[pid %d] reading %ld bytes from offset %ld\n", getpid(), size, offset);

  if (fseek(fp, offset, SEEK_SET) < 0) {
    printf("[pid %d] fseek error!\n", getpid());
  }

  while ((ch = getc(fp)) != EOF && rbytes < size) {
    if (ch != ' ' && ch != '\n') { ++count.charcount; }
    if (ch == ' ' || ch == '\n') { ++count.wordcount; }
    if (ch == '\n') { ++count.linecount; }
    rbytes++;
  }

  srand(getpid());
  if (CRASH > 0 && (rand() % 100 < CRASH)) {
    printf("[pid %d] crashed.\n", getpid());
    abort(); // simulate crash
  }

  return count;
}

typedef struct job_t {
  long offset;
  long size;
  int done;   // mark if job completed successfully
} job_t;

int run_child(const char *filename, job_t *job, int write_fd) {
  FILE *fp = fopen(filename, "r");
  if (!fp) exit(1);

  count_t result = word_count(fp, job->offset, job->size);
  write(write_fd, &result, sizeof(result));

  fclose(fp);
  return 0;
}

int main(int argc, char **argv) {
  long fsize;
  FILE *fp;
  int numJobs;
  count_t total = {0, 0, 0};

  if (argc < 3) {
    printf("usage: wc_mul <# of processes> <filename> [crash_rate]\n");
    return 0;
  }

  if (argc > 3) {
    CRASH = atoi(argv[3]);
    if (CRASH < 0) CRASH = 0;
    if (CRASH > 50) CRASH = 50;
  }
  printf("CRASH RATE: %d\n", CRASH);

  numJobs = atoi(argv[1]);
  if (numJobs > MAX_PROC) numJobs = MAX_PROC;

  fp = fopen(argv[2], "r");
  if (fp == NULL) {
    printf("File open error: %s\n", argv[2]);
    return 0;
  }

  fseek(fp, 0L, SEEK_END);
  fsize = ftell(fp);
  fclose(fp);

  long chunk_size = fsize / numJobs;

  // Prepare job list
  job_t jobs[numJobs];
  for (int i = 0; i < numJobs; i++) {
    jobs[i].offset = i * chunk_size;
    jobs[i].size = (i == numJobs - 1) ? (fsize - jobs[i].offset) : chunk_size;
    jobs[i].done = 0;
  }

  int remaining = numJobs;

  while (remaining > 0) {
    for (int i = 0; i < numJobs; i++) {
      if (jobs[i].done) continue; // skip completed jobs

      int pipefd[2];
      if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
      }

      pid_t pid = fork();
      if (pid < 0) {
        perror("fork");
        exit(1);
      } else if (pid == 0) {
        // Child
        close(pipefd[0]); // close read end
        run_child(argv[2], &jobs[i], pipefd[1]);
        close(pipefd[1]);
        exit(0);
      } else {
        // Parent
        close(pipefd[1]); // close write end
        count_t child_result;
        int status;

        // Wait specifically for this child
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
          if (read(pipefd[0], &child_result, sizeof(child_result)) > 0) {
            total.linecount += child_result.linecount;
            total.wordcount += child_result.wordcount;
            total.charcount += child_result.charcount;
          }
          jobs[i].done = 1;
          remaining--;
        } else if (WIFSIGNALED(status)) {
          printf("[parent] child %d crashed, redoing job %d\n", pid, i);
          // job[i].done remains 0, so it will retry in next iteration
        }

        close(pipefd[0]);
      }
    }
  }

  printf("\n========== Final Results ================\n");
  printf("Total Lines : %d \n", total.linecount);
  printf("Total Words : %d \n", total.wordcount);
  printf("Total Characters : %d \n", total.charcount);
  printf("=========================================\n");

  return 0;
}
