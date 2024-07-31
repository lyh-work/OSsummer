#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char* argv[]){
    int pid;
    int parent_pd[2]; // pd[0]代表管道接收端，pd[1]代表管道发送端
    int child_pd[2];
    char buf[10]; // 缓冲区
    //为父子进程建立管道
    pipe(child_pd); 
    pipe(parent_pd);
    pid = fork();
    //子进程
    if(pid == 0){
        read(parent_pd[0],buf, 4); // 从接收端读
        printf("%d: received %s\n",getpid(), buf);
        write(child_pd[1], "pong", sizeof(buf)); // 向发送端写
        exit(0);
    }
    //父进程
    else{
        write(parent_pd[1], "ping",4);
        read(child_pd[0], buf, sizeof(buf));
        printf("%d: received %s\n", getpid(), buf);
        exit(0);
    }
    
}
