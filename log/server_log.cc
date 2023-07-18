#include "server_log.h"

#include <sys/time.h>

using namespace std;

Log::Log() {
    lines_count_ = 0;
    is_async_ = false;
    write_thread_ = nullptr;
    log_block_queue_ = nullptr;
    date_of_today_ = 0;
    log_fp_ = nullptr;
}

Log::~Log() {
    if (write_thread_ && write_thread_->joinable()) {
        while (!log_block_queue_->empty()) {
            log_block_queue_->flush();
        }
        log_block_queue_->close();
        write_thread_->join();
    }
    if (log_fp_) {
        lock_guard<mutex> locker(mtx_);
        flush();
        fclose(log_fp_);
    }
}

int Log::GetLevel() {
    lock_guard<mutex> locker(mtx_);
    return level_;
}

void Log::SetLevel(int level) {
    lock_guard<mutex> locker(mtx_);
    level_ = level;
}


void Log::init(int level = 1, const char* path, 
                 const char* suffix, int max_queue_size) {
    // 參數初始化
    is_open_ = true;
    level_ = level;
    if (max_queue_size > 0) {
        is_async_ = true;
        if(!log_block_queue_){
            unique_ptr<BlockDeque<std::string>> new_queue(new BlockDeque<std::string>);
            log_block_queue_ = std::move(new_queue);
            std::unique_ptr<std::thread> new_thread(new thread(FlushLogThread));
            write_thread_ = std::move(new_thread);  
        }
    } else {
        is_async_ = false;
    }

    lines_count_ = 0;

    //获取当前时间
    time_t local_time = time(nullptr);
    struct tm *sysTime = localtime(&local_time);
    struct tm t = *sysTime;

    path_ = path;
    suffix_ = suffix;
    char log_full_name[LOG_NAME_LEN] = {0};

    snprintf(log_full_name, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
            path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_);

    date_of_today_ = t.tm_mday;

    //打开日志文件
    mtx_.lock();
    buffer_.RetrieveAll();
    if (log_fp_) {
        flush();
        fclose(log_fp_);
    }

    log_fp_ = fopen(log_full_name, "a");
    if (log_fp_ == nullptr) {
        mkdir(path_, 0777);
        log_fp_ = fopen(log_full_name, "a");
    }
    assert(log_fp_ != nullptr);
    mtx_.unlock();
}

void Log::AppendLogLevelTitle_(int level) {
    switch(level) {
        case 0:
            buffer_.Append("[debug]: ", 9);
            break;
        case 1:
            buffer_.Append("[info] : ", 9);
            break;
        case 2:
            buffer_.Append("[warn] : ", 9);
            break;
        case 3:
            buffer_.Append("[error]: ", 9);
            break;
        default:
            buffer_.Append("[info] : ", 9);
            break;
    }
}

void Log::write(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t local_time = now.tv_sec;
    struct tm *time_now = localtime(&local_time);
    struct tm time_log = *time_now;
    va_list value_list;

    //日志按照天数，最大行数进行切分
    if (date_of_today_ != time_log.tm_mday ||
        (lines_count_ && (lines_count_ % MAX_LINES == 0))) {

        char new_log[LOG_NAME_LEN] = {0};
        
        char tail[36] = {0};
        snprintf(tail, 36, "%04d_%02d_%02d_", time_log.tm_year + 1900, time_log.tm_mon + 1, time_log.tm_mday);
        if (date_of_today_ != time_log.tm_mday) {
            snprintf(new_log, LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            date_of_today_ = time_log.tm_mday;
            lines_count_ = 0;
        } else {
            snprintf(new_log, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lines_count_  / MAX_LINES), suffix_);
        }

        mtx_.lock();
        flush();
        fclose(log_fp_);
        log_fp_ = fopen(new_log, "a");
        assert(log_fp_ != nullptr);
        mtx_.unlock();
    }


    mtx_.lock();
    ++lines_count_;


    std::string log_str;

    int len_of_time = snprintf(buffer_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld",
                                time_log.tm_year + 1900, time_log.tm_mon + 1, time_log.tm_mday,
                                time_log.tm_hour, time_log.tm_min, time_log.tm_sec, now.tv_usec);
    buffer_.HasWritten(len_of_time);

    AppendLogLevelTitle_(level);

    va_start(value_list, format);
    int len_of_content = vsnprintf(buffer_.BeginWrite(), buffer_.WritableBytes(), format, value_list);
    va_end(value_list);

    buffer_.HasWritten(len_of_content);
    buffer_.Append("\n\0", 2);


    if (is_async_ && log_block_queue_ && !log_block_queue_->full()) {
        log_block_queue_->push_back(buffer_.RetrieveAllToStr());
    } else {
        char *buffer_start = const_cast<char *>(buffer_.Peek());
        printf("%s", buffer_start);
        fputs(buffer_start, log_fp_);
    }
    buffer_.RetrieveAll();
    mtx_.unlock();
}

void Log::flush() {
    if (is_async_) {
        log_block_queue_->flush();
    }
    fflush(log_fp_);
}

void Log::AsyncWrite_() {
    string str = "";
    while(log_block_queue_->pop(str)) {
        mtx_.lock();
        fputs(str.c_str(), log_fp_);
        mtx_.unlock();
    }
}

// 线程不断从阻塞队列中取出日志，写入文件
void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}
