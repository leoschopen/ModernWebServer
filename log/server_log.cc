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
    if (log_fp_ != nullptr) {
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


void Log::init(int level = 1, const char* file_name, int max_queue_size) {
    // 參數初始化
    is_open_ = true;
    level_ = level;
    if (max_queue_size > 0) {
        is_async_ = true;
        unique_ptr<BlockDeque<std::string>> new_queue(new BlockDeque<std::string>);
        log_block_queue_ = std::move(new_queue);
        std::unique_ptr<std::thread> new_thread(new thread(FlushLogThread));
        write_thread_ = std::move(new_thread);
    } else {
        is_async_ = false;
    }

    lines_count_ = 0;

    //获取当前时间
    time_t local_time = time(nullptr);
    struct tm *time_now = localtime(&local_time);
    struct tm time_log = *time_now;

    //获取日志文件名
    const char *p = strrchr(file_name, '/');
    char log_full_name[LOG_NAME_LEN] = {0};

    if (p == nullptr) {
        snprintf(log_full_name, LOG_NAME_LEN - 1, "%d_%02d_%02d_%s",
                 time_log.tm_year + 1900, time_log.tm_mon + 1, time_log.tm_mday, file_name);
    } else {
        strcpy(log_name_, p + 1);
        strncpy(dir_name_, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s",
                 dir_name_, time_log.tm_year + 1900, time_log.tm_mon + 1, time_log.tm_mday, log_name_);
    }

    date_of_today_ = time_log.tm_mday;

    //打开日志文件
    {
        lock_guard<mutex> locker(mtx_);
        buffer_.RetrieveAll();
        if (log_fp_) {
            flush();
            fclose(log_fp_);
        }

        log_fp_ = fopen(log_full_name, "a");
        if (log_fp_ == nullptr) {
            log_fp_ = fopen(log_full_name, "a");
        }
        assert(log_fp_ != nullptr);
    }
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
        unique_lock<mutex> locker(mtx_);

        char new_log[LOG_NAME_LEN] = {0};
        fflush(log_fp_);
        fclose(log_fp_);
        char tail[36] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", time_log.tm_year + 1900, time_log.tm_mon + 1, time_log.tm_mday);
        if (date_of_today_ != time_log.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name_, tail, log_name_);
            date_of_today_ = time_log.tm_mday;
            lines_count_ = 0;
        } else {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name_, tail, log_name_,
                     lines_count_ / MAX_LINES);
        }
        log_fp_ = fopen(new_log, "a");
        assert(log_fp_ != nullptr);
    }

    {
        unique_lock<mutex> locker(mtx_);
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
            log_block_queue_->push(buffer_.RetrieveAllToStr());
        } else {
            char *buffer_start = const_cast<char *>(buffer_.Peek());
            printf("%s", buffer_start);
            fputs(buffer_start, log_fp_);
        }
        buffer_.RetrieveAll();
    }
}

void Log::flush() {
    if (is_async_) {
        log_block_queue_->flush();
    }
    fflush(log_fp_);
}

void Log::AsyncWrite_() {
    string str;
    while(log_block_queue_->pop(str)) {
        lock_guard<mutex> locker(mtx_);
        fputs(str.c_str(), log_fp_);
    }
}

void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}
