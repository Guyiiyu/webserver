#ifndef THREADPOOL_H
#define THREADPOOL_H


#include <pthread.h>
#include <list>
#include <exception>
#include <pthread.h>
#include <cstdio>


#include "locker.h"

// 线程池类，模板类代码复用，T为任务
template<typename T>
class Threadpool {
public:

    Threadpool(int thread_number = 8, int max_requests = 10000);
    ~Threadpool();
    bool append(T* request);

private:
    static void * worker(void * arg);
    void run();

private:
    int m_thread_number;        // 线程数量
    pthread_t *m_threads;       // 线程池数组
    int m_max_request;          // 请求中最多允许等待的请求数量
    std::list<T*> m_workqueue;   // 请求队列
    Locker m_queuelocker;       // 请求队列互斥锁
    Sem m_queuestat;            // 信号量判断是否有任务需要处理
    bool m_stop;                // 是否结束线程  

};

#endif