#include "httpconn.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <strings.h>
#include <sys/ucontext.h>


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
    init();
}

void Httpconn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_checked_index = 0;
    m_start_line = 0;
    m_url = NULL;
    m_method = GET;
    m_versoin = 0;
    m_linger = false;
    m_host = NULL;
    bzero(m_read_buf, READ_BUFFER_SIZE);
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
    HTTP_CODE read_ret = process_read();
    if(read_ret == BAD_REQUEST) {
        modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    puts("解析http请求中");
    // 生成响应
}

// 主状态机，解析请求
Httpconn::HTTP_CODE Httpconn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char *text = 0;

    while( (m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
        ((line_status = parse_line()) == LINE_OK)) {
        // 解析到了一行完整的数据
        text = get_line();
        m_start_line = m_checked_index;
        printf("got 1 http line: %s\n", text);
        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
        return NO_REQUEST;
    }


    return NO_REQUEST;
}

// 解析HJTTP请求首行
Httpconn::HTTP_CODE Httpconn::parse_request_line(char *text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';

    char *method = text;
    if(!strcasecmp(method, "GET")) {
        m_method = GET;
    }
    else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    m_versoin = strpbrk(m_url, " \t");
    if(!m_versoin) {
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_versoin++ = '\0';
    if(strcasecmp(m_versoin, "HTTP/1.1")) {
        return BAD_REQUEST;
    }

    // http://192.168.1.1:80/index.html
    if(!strncasecmp(m_url, "http://", 7)) {
        // 192.168.1.1:80/index.html
        m_url += 7;
        // /index.html
        m_url = strchr(m_url, '/');
    }

     if(!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
     }
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

Httpconn::HTTP_CODE Httpconn::parse_headers(char *text) {
    return NO_REQUEST;
}

Httpconn::HTTP_CODE Httpconn::parse_content(char *text) {
    return NO_REQUEST;
}

// 获取一行，判断依据是\r\n
Httpconn::LINE_STATUS Httpconn::parse_line() {
    char temp;

    for( ; m_checked_index < m_read_idx; m_checked_index++) {
        temp = m_read_buf[m_checked_index];
        if(temp == '\r') {
            if(m_checked_index + 1 == m_read_idx) {
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_index + 1] == '\n') {
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n') {
            if(m_checked_index > 1 && m_read_buf[m_checked_index-1] == '\r') {
                m_read_buf[m_checked_index-1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        return LINE_OPEN;
    }
    return LINE_OK;
}

Httpconn::HTTP_CODE Httpconn::do_request() {
    return NO_REQUEST;
}