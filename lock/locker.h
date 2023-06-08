#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>

#include <exception>

class Semaphore {
   public:
    Semaphore() {
        if (sem_init(&sem_, 0, 0) != 0) {
            throw std::exception();
        }
    }
    explicit Semaphore(int num) {
        if (sem_init(&sem_, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~Semaphore() { sem_destroy(&sem_); }
    bool wait() { return sem_wait(&sem_) == 0; }
    bool post() { return sem_post(&sem_) == 0; }

   private:
    sem_t sem_;
};

class Locker {
   public:
    Locker() {
        if (pthread_mutex_init(&locker_mutex_, nullptr) != 0) {
            throw std::exception();
        }
    }
    ~Locker() { pthread_mutex_destroy(&locker_mutex_); }
    bool lock() { return pthread_mutex_lock(&locker_mutex_) == 0; }
    bool unlock() { return pthread_mutex_unlock(&locker_mutex_) == 0; }
    pthread_mutex_t *get() { return &locker_mutex_; }

   private:
    pthread_mutex_t locker_mutex_;
};

class Condition {
   public:
    Condition() {
        if (pthread_cond_init(&cond_, nullptr) != 0) {
            // pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~Condition() { pthread_cond_destroy(&cond_); }
    bool wait(pthread_mutex_t *m_mutex) {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&cond_, m_mutex);
        // pthread_mutex_unlock(&m_mutex);
        //  在成功完成之后会返回零。 其他任何返回值都表示出现了错误。
        return ret == 0;
    }
    bool time_wait(pthread_mutex_t *m_mutex, struct timespec t) {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&cond_, m_mutex, &t);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal() { return pthread_cond_signal(&cond_) == 0; }
    bool broadcast() { return pthread_cond_broadcast(&cond_) == 0; }

   private:
    // static pthread_mutex_t m_mutex;
    pthread_cond_t cond_;
};
#endif
