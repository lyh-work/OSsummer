#include "kernel/types.h"
#include "user/user.h"

#define PRIME_LIMIT 35

// 处理函数，递归处理管道中的数据
void
handle(int pfd) {
  int j, k;
  int spid;
  int sp[2];

  // 从管道读取数据
  if (read(pfd, &j, sizeof(int)) > 0) {
    // 打印当前素数
    fprintf(1, "prime %d\n", j);
    // 创建子管道
    pipe(sp);
    spid = fork();
    if (spid < 0) {
      // 创建子进程失败
      fprintf(2, "fork error\n");
      exit(1);
    } else if (spid == 0) {
      // 子进程，关闭写端并递归处理子管道
      close(sp[1]);
      handle(sp[0]);
    } else {
      // 父进程，关闭读端并将非倍数数据写入子管道
      close(sp[0]);
      while (read(pfd, &k, sizeof(int)) > 0) {
        // 如果k不是j的倍数，则将k写入子管道
        if (k % j != 0)
          write(sp[1], &k, sizeof(int));
      }
      // 关闭子管道的写端和父管道的读端
      close(sp[1]);
      close(pfd);
      // 等待子进程结束
      wait(0);
    }
  } else {
    // 如果从管道中没有读取到数据，关闭管道
    close(pfd);
  }
}

int
main() {
  int i;
  int pid;
  int p[2];
  // 创建管道
  pipe(p);
  pid = fork();

  if (pid < 0) {
    // 创建子进程失败
    fprintf(2, "fork error\n");
    exit(1);
  } else if (pid == 0) {
    // 子进程，关闭写端并处理管道
    close(p[1]);
    handle(p[0]);
  } else {
    // 父进程，关闭读端并将2到PRIME_LIMIT的数写入管道
    close(p[0]);
    for (i = 2; i <= PRIME_LIMIT; i++)
      write(p[1], &i, sizeof(int));
    // 关闭管道的写端
    close(p[1]);
    // 等待子进程结束
    wait(0);
  }

  exit(0);
}

