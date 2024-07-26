#include "kernel/types.h"
#include "user/user.h"

int 
main(int argc, char* argv[]){
    int pid;
    int parent_fd[2]; // 0代表管道接收端，1代表管道发送端
    int child_fd[2];
    char buf[10]; // 缓冲区
    //为父子进程建立管道
    pipe(child_fd); 
    pipe(parent_fd);
    pid = fork();
    //子进程
    if(pid == 0){
        close(parent_fd[1]);
        read(parent_fd[0],buf, 4); // 从接收端读
        printf("%d: received %s\n",getpid(), buf);
        close(child_fd[0]);
        write(child_fd[1], "pong", sizeof(buf)); // 向发送端写
        exit(0);
    }
    //父进程
    else{
        close(parent_fd[0]);
        write(parent_fd[1], "ping",4);
        close(child_fd[1]);
        read(child_fd[0], buf, sizeof(buf));
        printf("%d: received %s\n", getpid(), buf);
        exit(0);
    }
    
}
