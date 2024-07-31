#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

int
main(int argc, char *argv[]) {
    char buf[512];
    // 重新定义 argv[]->xargv[]
    char *xargv[MAXARG + 3];// xargs + exec + MAXARG(32个) + 0 结尾
    for (int i = 0; i < argc; ++i)   
        xargv[i] = argv[i];

    // 暂存 stdin 至 buf[]
    int i = 0;
    for (; i < sizeof(buf);) {
        char c;
        int n = read(0, &c, 1);
        if (n < 0)    
            exit(1);
        else if (n == 0)    
            break;
        buf[i++] = c;
    }
    buf[--i] = 0;// fd0 最后一个字符是回车，我这里覆盖掉，即改 CR->'\0'
    if (buf[0] == '"') {
        // 这块程序作用是："1\n2"->1\n2; "1\n2"hi"->"1\n2"
        // (只简单地取走前两个引号之间的字符，即使后面还有多的引号均忽略)
        int i = 1;
        for (; buf[i] != '"'; ++i)    buf[i - 1] = buf[i];
        buf[i] = 0;
        buf[i - 1] = 0;

        // 转化转义字符，但只处理 \n
        int size = strlen(buf);
        for (i = 0; i < size; ++i) {
            if (buf[i] == '\\'&& buf[i + 1] == 'n') {
                buf[i] = 0x0A;  // ASCII 0x0a = 回车
                memmove(&buf[i + 1], &buf[i + 2], size - i - 2);
                buf[size - 1] = 0;
                --size;
            }
        }
    }

    // buf[] 追加到 xargv[]
    char *p = buf;
    for (i = 0; i < strlen(buf) && argc < MAXARG + 2; ++i) {
        int j = 0;
        for (; i < strlen(buf); ++i, ++j)
            if (buf[i] == ' ' || buf[i] == '\n')    
                break;
        xargv[argc] = malloc(j + 1);
        memmove(xargv[argc], p, j);
        xargv[argc++][j] = 0;
        p = p + j + 1;
    }
    xargv[argc] = 0;

    // 将 xargv[] 移交 exec)
    int pid = fork();
    if (pid < 0) {
        fprintf(2, "xargs: cannot xargs\n");
        exit(1);
    } else if (pid > 0) {
        wait(0);
        exit(0);
    } else {
        exec(xargv[1], &xargv[1]);// xargv[0] 只是 "xargs"，所以取再后一个
        exit(1);// exec 是不会返回的，唯一会返回的情况是出错了
    }
}