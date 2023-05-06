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
        if(pthread_create(m_threads+i, NULL, worker, this)) {
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
    Threadpool<T> * pool = (Threadpool<T> *) arg;
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

        request->pocess();

    }
}