#include "kernel/types.h" // 引入声明类型的头文件
#include "user/user.h" // 引入声明系统调用的头文件

int
main(int argc, char *argv[])
{
    if (argc != 2) //参数数量错误
    {
        printf("usage: sleep seconds\n");
        // 使用 user/printf.c 中的 printf(const char *fmt, ...) 函数进行格式化输出
        exit(-1);
    }
    sleep(atoi(argv[1]));
    exit(0);
}