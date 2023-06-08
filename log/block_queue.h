#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <pthread.h>
#include <cstdlib>
#include <sys/time.h>

#include <iostream>

#include "../lock/locker.h"

template<class T>
class BlockQueue {
public:
    explicit BlockQueue(int max_size = 1000) {
        if (max_size <= 0) {
            exit(-1);
        }

        block_queue_max_size_ = max_size;
        block_queue_size_ = 0;
        block_queue_front_ = -1;
        block_queue_back_ = -1;
        block_queue_array_ = new T[max_size];
    }

    ~BlockQueue(){
        block_queue_mutex_.lock();
        if (block_queue_array_ != NULL) {
            delete[] block_queue_array_;
        }
        block_queue_mutex_.unlock();
    }

    void clear(){
        block_queue_mutex_.lock();
        block_queue_size_ = 0;
        block_queue_front_ = -1;
        block_queue_back_ = -1;
        block_queue_mutex_.unlock();
    }

    bool full() {
        block_queue_mutex_.lock();
        if (block_queue_size_ >= block_queue_max_size_) {
            block_queue_mutex_.unlock();
            return true;
        }
        block_queue_mutex_.unlock();
        return false;
    }
    // 判断队列是否为空
    bool empty(){
        block_queue_mutex_.lock();
        if (block_queue_size_ == 0) {
            block_queue_mutex_.unlock();
            return true;
        }
        block_queue_mutex_.unlock();
        return false;
    }

    // 返回队首元素
    bool front(T &value){
        block_queue_mutex_.lock();
        if (block_queue_size_ == 0) {
            block_queue_mutex_.unlock();
            return false;
        }
        value = block_queue_array_[block_queue_front_];
        block_queue_mutex_.unlock();
        return true;
    }

    // 返回队尾元素
    bool back(T &value){
        block_queue_mutex_.lock();
        if (block_queue_size_ == 0) {
            block_queue_mutex_.unlock();
            return false;
        }
        value = block_queue_array_[block_queue_back_];
        block_queue_mutex_.unlock();
        return true;
    }

    int size(){
        int tmp = 0;
        block_queue_mutex_.lock();
        tmp = block_queue_size_;
        block_queue_mutex_.unlock();
        return tmp;
    }

    int max_size(){
        int tmp = 0;
        block_queue_mutex_.lock();
        tmp = block_queue_max_size_;
        block_queue_mutex_.unlock();
        return tmp;
    }

    // 往队列添加元素，需要将所有使用队列的线程先唤醒
    bool push(const T &item){
        block_queue_mutex_.lock();
        if (block_queue_size_ >= block_queue_max_size_) {
            block_queue_cond_.broadcast();
            block_queue_mutex_.unlock();
            return false;
        }
        block_queue_back_ = (block_queue_back_ + 1) % block_queue_max_size_;
        block_queue_array_[block_queue_back_] = item;
        block_queue_size_++;
        block_queue_cond_.broadcast();
        block_queue_mutex_.unlock();
        return true;
    }

    bool pop(T &item){
        block_queue_mutex_.lock();
        while (block_queue_size_ <= 0) {
            if (!block_queue_cond_.wait(block_queue_mutex_.get())) {
                block_queue_mutex_.unlock();
                return false;
            }
        }
        block_queue_front_ = (block_queue_front_ + 1) % block_queue_max_size_;
        item = block_queue_array_[block_queue_front_];
        block_queue_size_--;
        block_queue_mutex_.unlock();
        return true;
    }

    bool pop(T &item, int ms_timeout){
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, nullptr);
        block_queue_mutex_.lock();
        if (block_queue_size_ <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!block_queue_cond_.time_wait(block_queue_mutex_.get(), t)) {
                block_queue_mutex_.unlock();
                return false;
            }
        }
        if (block_queue_size_ <= 0) {
            block_queue_mutex_.unlock();
            return false;
        }
        block_queue_front_ = (block_queue_front_ + 1) % block_queue_max_size_;
        item = block_queue_array_[block_queue_front_];
        block_queue_size_--;
        block_queue_mutex_.unlock();
        return true;
    }

private:
    Locker block_queue_mutex_;
    Condition block_queue_cond_;

    T *block_queue_array_;
    int block_queue_size_;
    int block_queue_max_size_;
    int block_queue_front_;
    int block_queue_back_;
};


#endif