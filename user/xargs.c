#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define BUFSIZE 30

int
main(int argc, char *argv[]) {
  int i;
  int j = 0;
  int pid;
  char buf[BUFSIZE];
  char *inst_args[MAXARG];
  for (i = 1; i < argc; i++) {
    inst_args[i - 1] = (char*)malloc(BUFSIZE);
    inst_args[i - 1] = argv[i];
  }
  i = argc - 1;
  while ((read(0, &buf[j], 1)) > 0) {
    if (buf[j] == ' ') {
      buf[j] = 0;
      inst_args[i] = (char*)malloc(BUFSIZE);
      memmove(inst_args[i], buf, j);
      j = 0;
      i++;
      continue;
    } else if (buf[j] == '\n') {
      buf[j] = 0;
      inst_args[i] = (char*)malloc(BUFSIZE);
      memmove(inst_args[i], buf, j);
      pid = fork();
      if (pid < 0) {
        fprintf(2, "fork error\n");
        exit(1);
      } else if (pid == 0) {
        exec(inst_args[0], inst_args);
        exit(0);
      } else {
        // pid > 0
        for (j = argc - 1; j < i + 1; j++) {
          // reset inst_args
          inst_args[j] = 0;
        }
        i = argc - 1;
        j = 0;
        wait(&pid);
        continue;
      }
    }
    j++;
  }

  exit(0);
} 