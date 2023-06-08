#ifndef LOG_H
#define LOG_H

#include <pthread.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <iostream>
#include <string>

#include "block_queue.h"

class Log {
public:
    static Log *get_instance() {
        static Log log_instance;
        return &log_instance;
    }

    static void *flush_log_thread(void *args) {
        Log::get_instance()->async_write_log();
        return nullptr;
    }

    bool init(const char *file_name, int log_buf_size = 8192,
              int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush();

private:
    Log();

    virtual ~Log();

    // 异步写日志，声明为私有，不允许外部调用，只能通过flush_log_thread()调用
    void *async_write_log() {
        std::string single_log;
        // 从阻塞队列中取出一个日志string，写入文件
        while (log_block_queue_->pop(single_log)) {
            log_mutex_.lock();
            fputs(single_log.c_str(), log_fp_);
            log_mutex_.unlock();
        }
    }

private:
    char dir_name_[128]{};     // 路径名
    char log_name_[128]{};     // log文件名
    int max_split_lines_{};    // 日志最大行数
    int log_buf_size_{};       // 日志缓冲区大小
    long long lines_count_;  // 日志行数记录
    int date_of_today_{};
    FILE *log_fp_{};
    char *buffer_{};
    BlockQueue<std::string> *log_block_queue_{};  // 阻塞队列
    bool is_async_;                              // 是否同步标志位
    Locker log_mutex_;
};

#define LOG_DEBUG(format, ...) \
    Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) \
    Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) \
    Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) \
    Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif