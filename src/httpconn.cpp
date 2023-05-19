#include "httpconn.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ucontext.h>

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* doc_root = "/home/ubuntu/www";

int Httpconn::m_epollfd = -1;
int Httpconn::m_user_count = 0;

void setnoblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    int ret = fcntl(fd, F_SETFL, new_flag);
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
    ev.events = event | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
} 

// 初始化连接
void Httpconn::init(int sockfd, const sockaddr_in &addr) {
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
    bytes_have_send = 0;
    bytes_to_send = 0; 

    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = NULL;
    m_version = NULL;
    m_content_len = 0;
    m_host = NULL;
    m_checked_index = 0;
    m_start_line = 0;

    m_read_idx = 0;
    m_write_idx = 0; 

    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

// 关闭连接
void Httpconn::close_conn() {
    printf("关闭socket %d \n", m_sockfd);
    if(m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool Httpconn::read() {
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
        m_read_idx += bytes_read;
    }

    printf("读取到的数据 %s \n", m_read_buf);

    return true;
}


// 主状态机，解析请求
Httpconn::HTTP_CODE Httpconn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    int cnt = 0;

    char *text = 0;
    while( (m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
        ((line_status = parse_line()) == LINE_OK)) {
        if(cnt++ >= 100) break;
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
    }
    return NO_REQUEST;
}

// 解析HJTTP请求首行
Httpconn::HTTP_CODE Httpconn::parse_request_line(char *text) {
    
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    printf("sockfd = %d read_buf = %lld m_url=%lld\nrequest_line: %s\n", m_sockfd, (long long)m_read_buf, (long long)m_url, text);
    if(m_url == NULL) {
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';
    printf("url = %s\n", m_url);
    

    char *method = text;
    
    if(!strcasecmp(method, "GET")) {
        m_method = GET;
    }
    else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    if(strcasecmp(m_version, "HTTP/1.1")) {
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
    // 遇到空行表示头部字段解析完毕
    if(text[0] == '\0') {
        // 表示有请求体
        if(m_content_len) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    // 处理 Connection: keep-alive
    else if(strncasecmp(text, "Connection: ", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "kep-alive") == 0) {
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_len = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        printf("ohh, unknow header\"%s\"\n", text);
    }
    return NO_REQUEST;
}

// 仅判断是否完整读入
Httpconn::HTTP_CODE Httpconn::parse_content(char *text) {
    if(m_read_idx >= (m_content_len + m_checked_index)) {
        text[m_content_len] = '\0';
        return GET_REQUEST;
    }
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
    }
    return LINE_OK;
}

// 分析目标文件属性，如果目标文件存在不是目录，且可读
// 则使用mmap将其用射到内存地址m_file_address处
Httpconn::HTTP_CODE Httpconn::do_request() {
    // 获取目标文件绝对路径
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    printf("filepath = %s\n", m_real_file);

    // 获取目标文件相关信息
    if( stat(m_real_file, &m_file_stat) < 0 ) {
        return NO_RESOURCE;
    }
    // 判断是否有权限
    if(!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    // 判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    m_file_address = (char *)mmap(NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void Httpconn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = NULL;
    }
}

// 在写缓冲区中写入待发送数据
bool Httpconn::add_response( const char* format, ... ) {
    if(m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx,  format, arg_list);
    if(len >= WRITE_BUFFER_SIZE - 1 - m_write_idx) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}


bool Httpconn::add_status_line( int status, const char* title ) {
    return add_response("%s %d %d\r\n", "HTTP/1.1", status, title);
}

bool Httpconn::add_headers( int content_length ) {
    bool ret = true;
    ret &= add_content_length(content_length);
    ret &= add_content_type();
    ret &= add_linger();
    ret &= add_blank_line();
    return ret;
}

bool Httpconn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool Httpconn::add_content_length( int content_length ) {
    return add_response( "Content-Length: %d\r\n", content_length );
}

bool Httpconn::add_linger() {
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool Httpconn::add_blank_line() {
    return add_response( "%s", "\r\n" );
}

bool Httpconn::add_content( const char* content ) {
    return add_response( "%s", content );
}

bool Httpconn::process_write(Httpconn::HTTP_CODE ret) {
    switch(ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 写HTTP响应
bool Httpconn::write() {
    int temp = 0;
    if(bytes_to_send == 0) {
        modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(true) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1) {
            // 如果TCP没有写缓冲空间
            if(errno == EAGAIN) {
                modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if(bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        // 发送完数据
        if(bytes_to_send <= 0) {
            unmap();
            modifyfd(m_epollfd, m_sockfd, EPOLLIN);
            if(m_linger) {
                init();
                return true;
            }
            else {
                return false;
            }
        }

    }
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
    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    modifyfd(m_epollfd, m_sockfd, EPOLLOUT);
    puts("生成响应");
    printf("read ret = %d write ret=%d\n", read_ret, write_ret);
    printf("响应头：%s\n", m_write_buf);
}