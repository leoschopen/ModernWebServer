#ifndef LOG_H
#define LOG_H

#include <pthread.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>         //mkdir

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

    void init(int level, const char *path = "../ServerLog", 
                const char* suffix =".log",int max_queue_size = 1024);

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

    const char *path_; 
    const char *suffix_;      // log后缀名


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

#define LOG_BASE(level, format, ...) \
    do {\
        Log* log = Log::Instance();\
        if (log->IsOpen() && log->GetLevel() <= level) {\
            log->write(level, format, ##__VA_ARGS__); \
            log->flush();\
        }\
    } while(0);

#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);

#endif