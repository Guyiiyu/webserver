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
    bool append(T* request);    // 添加新的任务

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

#include "threadpool.h"

template<typename T>
Threadpool<T>::Threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_request(max_requests),
    m_stop(false), m_threads(NULL) {
    if((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) {
        throw std::exception();
    }

    // 创建线程，设置为分离
    for(int i=0; i<thread_number; i++ ) {
        printf("create the %dth thread\n", i);
        if(pthread_create(m_threads+i, NULL, worker, this) != 0) {
            delete [] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i])) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
Threadpool<T>::~Threadpool() {
    delete [] m_threads;
    m_stop = false;
}

template<typename T>
bool Threadpool<T>::append(T* request) {
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_request) {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
template<typename T>
void * Threadpool<T>::worker(void * arg) {
    // Threadpool<T> * pool = (Threadpool<T> *) arg;
    Threadpool *pool = (Threadpool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void Threadpool<T>::run() {
    while(!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request) {
            continue;
        }

        request->process();

    }
}

#endif