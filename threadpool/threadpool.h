//
// Created by leo on 23-6-3.
//

#ifndef SERVER_THREADPOOL_H
#define SERVER_THREADPOOL_H
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class ThreadPool {
public:
    ThreadPool() = default;

    ThreadPool(SqlConnectionPool *connectionPool, int curThreadNumber = 8, int maxRequests = 10000);

    ~ThreadPool();
    bool append(T *request);
private:
    static void *worker(void *arg);
    void run();
private:
    int thread_number_;        //线程池中的线程数
    int max_requests_;         //请求队列中允许的最大请求数
    pthread_t *threads_;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> request_queue_; //请求队列
    Locker queue_locker_;       //保护请求队列的互斥锁
    Semaphore queue_status_;            //是否有任务需要处理
    bool stop_;                //是否结束线程
    SqlConnectionPool *connection_pool_;  //数据库
};

template<typename T>
ThreadPool<T>::ThreadPool(SqlConnectionPool *connectionPool,int threadNumber, int maxRequests):
                          thread_number_(threadNumber),max_requests_(maxRequests),
                          connection_pool_(connectionPool),stop_(false),threads_(nullptr){
    if(threadNumber<=0 || maxRequests<=0) {
        throw std::exception();
    }
    threads_ = new pthread_t[thread_number_];
    if(!threads_) {
        throw std::exception();
    }
    for(int i=0;i<threadNumber;i++) {
        if(pthread_create(threads_+i, nullptr, worker, this)!=0) {
            delete [] threads_;
            throw std::exception();
        }
        // 如果线程是 detached，则资源会随着线程函数结束，自动释放
        if(pthread_detach(threads_[i])) {
            delete [] threads_;
            throw std::exception();
        }
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] threads_;
    stop_ = true;
}

template<typename T>
void ThreadPool<T>::run() {
    while(!stop_){
        queue_status_.wait();
        queue_locker_.lock();
        if(request_queue_.empty()) {
            queue_locker_.unlock();
            continue;
        }
        T *request = request_queue_.front();
        request_queue_.pop_front();
        queue_locker_.unlock();
        if(!request) {
            continue;
        }
        ConnectionRAII mysqlConnection(&request->mysql_, connection_pool_);
        request->process();
    }
}

template<typename T>
void *ThreadPool<T>::worker(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}

template<typename T>
bool ThreadPool<T>::append(T *request) {
    queue_locker_.lock();
    if(request_queue_.size()>max_requests_) {
        queue_locker_.unlock();
        return false;
    }
    request_queue_.push_back(request);
    queue_locker_.unlock();
    queue_status_.post();
    return true;
}




#endif //SERVER_THREADPOOL_H
