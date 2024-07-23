#include "kernel/types.h" // 引入声明类型的头文件
#include "user/user.h" // 引入声明系统调用的头文件

int main(int argc, char *argv[])
{
    if (argc != 2) //参数数量错误
    {
        fprintf(2, "usage: sleep seconds\n");
        // 使用 user/printf.c 中的 fprintf(int fd, const char *fmt, ...) 函数进行格式化输出
        // 参数 fd 是文件描述符，0 表示标准输入，1 表示标准输出，2 表示标准错误
        exit(-1);
    }
    sleep(atoi(argv[1]));
    exit(0);
}