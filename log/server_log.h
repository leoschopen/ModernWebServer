#ifndef LOG_H
#define LOG_H

#include <pthread.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <iostream>
#include <string>
#include <memory>
#include <mutex>
#include <thread>

#include "blockqueue.h"
#include "../buffer/buffer.h"

class Log {
public:
    static Log *Instance(){
        static Log instance;
        return &instance;
    }
    static void FlushLogThread();

    void init(int level, const char *file_name = "ServerLog", int max_queue_size = 0);

    void write(int level, const char *format, ...);
    void flush();

    int GetLevel();
    void SetLevel(int level);
    bool IsOpen() const { return is_open_; }

private:
    Log();
    void AppendLogLevelTitle_(int level);
    virtual ~Log();
    // 异步写日志，声明为私有，不允许外部调用，只能通过flush_log_thread()调用
    void AsyncWrite_();

private:
    static const int LOG_PATH_LEN = 256;
    static const int LOG_NAME_LEN = 256;
    static const int MAX_LINES = 50000;

    char dir_name_[128];     // 路径名
    char log_name_[128];     // log文件名

    bool is_open_;           // 是否打开日志
    long long lines_count_;  // 日志行数记录

    int date_of_today_;

    Buffer buffer_;
    int level_;
    bool is_async_;// 是否同步标志位

    FILE *log_fp_;
    std::unique_ptr<BlockDeque<std::string>> log_block_queue_; // 阻塞队列
    std::unique_ptr<std::thread> write_thread_;
    std::mutex mtx_;
};

#define LOG_DEBUG(format, ...) \
    Log::Instance()->write(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) \
    Log::Instance()->write(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) \
    Log::Instance()->write(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) \
    Log::Instance()->write(3, format, ##__VA_ARGS__)

#endif