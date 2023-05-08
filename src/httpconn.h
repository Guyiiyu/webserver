#ifndef HTTPCONN_H
#define HTTPCONN_H

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include <signal.h>
#include <fcntl.h>

#include "locker.h"
#include "threadpool.h"

class Httpconn {
public:
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    Httpconn() = default;
    ~Httpconn() = default;

    void process();                                 // 处理客户端请求
    void init(int sockfd, sockaddr_in &addr);       // 初始化新连接
    void close_conn();                              // 关闭连接 
    bool read();                                    // 非阻塞读
    bool write();                                   // 非阻塞写
   

public:
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 2048;
    static int m_epollfd;
    static int m_user_count;

    

private:
    int m_sockfd;
    sockaddr_in m_addr;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;                         // 标识该读的起始下标
    char m_write_buf[WRITE_BUFFER_SIZE];

    int m_checked_index;                    // 下一个该从缓冲区取字符的位置
    int m_start_line;                       // 当前解析的行的起始位置
    char *m_url;                            // 请求目标的目标文件名
    char *m_versoin;                        // 协议版本
    METHOD m_method;                        // 请求方法
    char *m_host;                           // 主机名
    bool m_linger;                          // 是否保持连接

    CHECK_STATE m_check_state;              // 主状态机所处状态



private:
    HTTP_CODE process_read();                       // 解析HTTP请求
    HTTP_CODE parse_request_line(char *text);       // 解析首行
    HTTP_CODE parse_headers(char *text);            // 解析请求头
    HTTP_CODE parse_content(char *text);            // 解析请求体

    LINE_STATUS parse_line();
    void init();                                    // 初始化一些信息
    inline char *get_line() {
        return m_read_buf + m_start_line;
    }
    HTTP_CODE do_request();
};



#endif