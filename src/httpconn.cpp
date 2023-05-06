#include "httpconn.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>


int Httpconn::m_epollfd = -1;
int Httpconn::m_user_count = 0;

void setnoblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    int ret = fcntl(fd, F_SETFL, new_flag);
    printf("set fd=%d to nonblocking, ret = %d\n", fd, ret);
}

// 将fd添加到epoll中
void addfd(int epollfd, int fd, bool oneshot) {
    epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(oneshot) {
        ev.events |= EPOLLONESHOT;
    }
    int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    printf("add fd=%d to epoll=%d, ret=%d\n", fd, epollfd, ret);
    // 设置文件描述符非阻塞
    setnoblocking(fd);
}

// 从epoll中删除并关闭fd
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}

// 修改fd的事件监听为event | oneshot
void modifyfd(int epollfd, int fd, int event) {
    epoll_event ev;
    ev.data.fd = fd;
    ev.events = event | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
} 

// 初始化连接
void Httpconn::init(int sockfd, sockaddr_in &addr) {
    m_addr = addr;
    m_sockfd = sockfd;

    // 端口复用
    int opt = 1;
    int ret = setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 添加到epoll对象中
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
}

// 关闭连接
void Httpconn::close_conn() {
    if(m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool Httpconn::read() {
    puts("一次性读完所有数据");
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    while(true) {
        int bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1) {
            // 没有数据
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        // 对方关闭连接
        else if(bytes_read == 0) {
            return false;
        }
        m_read_idx += m_read_idx;
    }

    printf("读取到的数据 %s \n", m_read_buf);

    return true;
}

bool Httpconn::write() {
    puts("一次性写完所有数据");
    return true;
}

// 由线程池中的线程调用
void Httpconn::process() {
    // 解析HTTP请求
    puts("解析http请求中");
    // 生成响应
}