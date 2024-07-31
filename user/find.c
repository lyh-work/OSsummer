#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 在给定路径中查找文件
void
find(char *path, char *filename) {
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  // 打开路径
  if((fd = open(path, 0)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  // 获取路径的状态信息
  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';
  
  // 读取目录项
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if(de.inum == 0)
      continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = 0;

    // 获取文件状态
    if(stat(buf, &st) < 0){
      printf("ls: cannot stat %s\n", buf);
      continue;
    }

    switch (st.type) {
    case T_FILE:
      // 如果找到匹配的文件，打印其路径
      if (strcmp(filename, de.name) == 0)
        fprintf(1, "%s\n", buf);
      break;

    case T_DIR:
      // 忽略"."和".."目录，递归查找子目录
      if (strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0)
        find(buf, filename);
      break;
    }
  }
  close(fd);
}

int
main(int argc, char *argv[]) {
  char *path;
  char *filename;

  // 检查参数数量
  if (argc < 3) {
    fprintf(1, "type find <path> <filename>...\n");
    exit(0);
  }

  path = argv[1];
  filename = argv[2];

  // 调用查找函数
  find(path, filename);
  exit(0);
}