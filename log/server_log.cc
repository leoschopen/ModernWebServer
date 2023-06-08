#include "server_log.h"

#include <sys/time.h>

Log::Log() {
    lines_count_ = 0;
    is_async_ = false;
}

Log::~Log() {
    if (log_fp_ != nullptr) {
        fclose(log_fp_);
    }
}

bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size) {
    if (max_queue_size >= 1) {
        is_async_ = true;
        log_block_queue_ = new BlockQueue<std::string>(max_queue_size);
        pthread_t tid;
        pthread_create(&tid, nullptr, flush_log_thread, nullptr);
    }

    log_buf_size_ = log_buf_size;
    buffer_ = new char[log_buf_size_];
    memset(buffer_, '\0', log_buf_size_);
    max_split_lines_ = split_lines;

    //获取当前时间
    time_t local_time = time(nullptr);
    struct tm *time_now = localtime(&local_time);
    struct tm time_log = *time_now;

    //获取日志文件名
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == nullptr) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s",
                 time_log.tm_year + 1900, time_log.tm_mon + 1, time_log.tm_mday, file_name);
    } else {
        strcpy(log_name_, p + 1);
        strncpy(dir_name_, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s",
                 dir_name_, time_log.tm_year + 1900, time_log.tm_mon + 1, time_log.tm_mday, log_name_);
    }

    date_of_today_ = time_log.tm_mday;

    //打开日志文件
    log_fp_ = fopen(log_full_name, "a");
    if (log_fp_ == nullptr) {
        return false;
    }
    return true;
}

void Log::write_log(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t local_time = now.tv_sec;
    struct tm *time_now = localtime(&local_time);
    struct tm time_log = *time_now;

    char level_name[16] = {0};
    switch (level) {
        case 0:
            strcpy(level_name, "[debug]:");
            break;
        case 1:
            strcpy(level_name, "[info]:");
            break;
        case 2:
            strcpy(level_name, "[warn]:");
            break;
        case 3:
            strcpy(level_name, "[error]:");
            break;
        default:
            strcpy(level_name, "[info]:");
            break;
    }

    //写入日志
    log_mutex_.lock();
    lines_count_++;

    //日志按照天数，最大行数进行切分
    if (date_of_today_ != time_log.tm_mday ||
        lines_count_ % max_split_lines_ == 0) {
        char new_log[256] = {0};
        fflush(log_fp_);
        fclose(log_fp_);
        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", time_log.tm_year + 1900, time_log.tm_mon + 1, time_log.tm_mday);
        if (date_of_today_ != time_log.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name_, tail, log_name_);
            date_of_today_ = time_log.tm_mday;
            lines_count_ = 0;
        } else {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name_, tail, log_name_,
                     lines_count_ / max_split_lines_);
        }
        log_fp_ = fopen(new_log, "a");
    }

    log_mutex_.unlock();

    va_list value_list;
    va_start(value_list, format);

    std::string log_str;
    log_mutex_.lock();

    int len_of_time = snprintf(buffer_, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                               time_log.tm_year + 1900, time_log.tm_mon + 1, time_log.tm_mday,
                               time_log.tm_hour, time_log.tm_min, time_log.tm_sec, now.tv_usec, level_name);

    int len_of_content = vsnprintf(buffer_ + len_of_time, log_buf_size_ - 1, format, value_list);

    buffer_[len_of_time + len_of_content] = '\n';
    buffer_[len_of_time + len_of_content + 1] = '\0';
    log_str = buffer_;

    log_mutex_.unlock();

    if (is_async_ && !log_block_queue_->full()) {
        log_block_queue_->push(log_str);
    } else {
        log_mutex_.lock();
        fputs(log_str.c_str(), log_fp_);
        log_mutex_.unlock();
    }

    va_end(value_list);
}

void Log::flush() {
    log_mutex_.lock();
    fflush(log_fp_);
    log_mutex_.unlock();
}