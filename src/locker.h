#ifndef LOCKER_H
#define LOCKER_H

#include <bits/types/struct_timespec.h>
#include <exception>

#include <pthread.h>
#include <semaphore.h>

// 线程同步机制封装类

// 互斥锁类
class Locker {
public:
    Locker();
    ~Locker();

    bool lock();
    bool unlock();

    pthread_mutex_t *get();

private:
    pthread_mutex_t m_mutex;
};

// 条件变量类
class Cond {
public:

    Cond();
    ~Cond();

    bool wait(pthread_mutex_t *mutex);
    bool timedwait(pthread_mutex_t *mutex, struct timespec t);

    bool signal();
    bool broadcast();


private:
    pthread_cond_t m_cond;
};


// 信号量类
class Sem {
public:
    Sem(int num=0);
    ~Sem();

    bool wait();
    bool post();

private:
    sem_t m_sem;

};

#endif