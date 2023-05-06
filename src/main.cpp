#include <asm-generic/errno-base.h>
#include <cerrno>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include <signal.h>

#include "locker.h"
#include "threadpool.h"
#include "httpconn.h"


const int MAX_FD = 65535;
const int MAX_EVENT_NUMBER = 65535;

void addsig(int sig, void (handler)(int)) {
    struct sigaction sa;
    bzero(&sa, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

extern void addfd(int epollfd, int fd, bool oneshot);
extern void removefd(int epollfd, int fd);
extern void modifyfd(int epollfd, int fd, int event);

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("useage: %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    // 获取监听端口
    int port = atoi(argv[1]);
    if(port < 0 || port > 65535) {
        printf("port_number error!\n");
        exit(-1);
    }

    // 添加信号捕捉
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池
    Threadpool<Httpconn> * pool = NULL;
    try {
        pool = new Threadpool<Httpconn>;
    } catch (...) {
        exit(-1);
    }
    // 用来保存所有客户端信息
    Httpconn * users = new Httpconn[MAX_FD];


    // 获取监听端口
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if(listenfd == -1 ) {
        perror("create listen socket error");
        exit(-1);
    }

    // 设置端口复用
    int opt = 1;
    int ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(ret == -1) {
        perror("port reuse");
        exit(-1);
    }

    // 绑定端口
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr.s_addr);
    ret = bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));
    if(ret == -1) {
        perror("bind listen port");
        exit(-1);
    }
    // 监听
    ret = listen(listenfd, 5);
    if(ret == -1) {
        perror("listen");
        exit(-1);
    }

    // 设置epoll监听
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(6);
    if(epollfd == -1) {
        perror("create epoll");
        exit(-1);
    }
    Httpconn::m_epollfd = epollfd;
    
    // 将监听fd放入epoll中
    {
        epoll_event ev;
        ev.data.fd = listenfd;
        ev.events = EPOLLIN | EPOLLRDHUP;
        int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &ev);
        // 设置文件描述符非阻塞
        int old_flag = fcntl(listenfd, F_GETFL);
        int new_flag = old_flag | O_NONBLOCK;
        fcntl(listenfd, F_SETFL, new_flag);
    }
    // addfd(epollfd, listenfd, false);

    
    while(true) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(num < 0 && errno != EINTR) {
            perror("epoll wait");
            break;
        }
        for(int i=0; i<num; i++) {
            epoll_event &ev = events[i];
            int fd = ev.data.fd;
            // 检测到新的客户端连接
            if(fd == listenfd) {
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int connfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_addr_len);
                printf("获取到新的client fd = %d\n", connfd);
                if(Httpconn::m_user_count >= MAX_FD) {
                    // 连接数已满
                    close(connfd);
                    continue;
                }
                // 将新的客户端数据初始化，并保存下来
                users[connfd].init(connfd, client_addr);
            }
            // 对方异常断开
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                users[i].close_conn();
            }
            else if(events[i].events & EPOLLIN) {
                // 一次性把所有数据都读完
                if(users[fd].read()) {
                    pool->append(users + fd);
                }
                else {
                    users[fd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT) {
                if(!users[fd].write()) {
                    users[fd].close_conn();
                }
            }

        }

    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}