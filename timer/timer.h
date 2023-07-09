//
// Created by leo on 23-6-2.
//

#ifndef SERVER_SERVER_TIMER_H
#define SERVER_SERVER_TIMER_H

#include <netinet/in.h>
#include <queue>
#include <unordered_map>
#include <ctime>
#include <algorithm>
#include <arpa/inet.h>
#include <functional>
#include <cassert>
#include <chrono>
#include <vector>
#include <memory>
#include <set>
#include "../log/server_log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

class TimerManger;
class Timer;

struct ClientData {
    sockaddr_in address;
    int sockfd;
    Timer *timer;
};

class Timer : public std::enable_shared_from_this<Timer> {
    friend class TimerManger;
public:
    typedef std::shared_ptr<Timer> ptr;
    bool cancel(); // 取消定时器
    bool refresh(); // 刷新定时器的执行时间，改为当前时间+ms
    bool reset(uint64_t ms, bool from_now); // 重置定时器时间
private:
    Timer(uint64_t ms, TimeoutCallBack cb, bool recurring, TimerManger* manager);
    Timer(TimeStamp expires);
private:
    struct Comparator {
        bool operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const;
    };
public:
    ClientData *client_data; // 客户端数据
private:
    bool recurring_; // 是否循环定时器
    uint64_t ms_; // 执行间隔时间
    TimeStamp expires_; // 执行时间
    TimeoutCallBack cb_; // 回调函数
    TimerManger* manager_ = nullptr; // 定时器管理器
    std::mutex mtx_;
};

class TimerManger {
    friend class Timer;
public:
    TimerManger(): previouse_time_(Clock::now()) {};
    virtual ~TimerManger() = default;
    Timer::ptr addTimer(uint64_t ms, const TimeoutCallBack& cb,
                        bool recurring = false);
    Timer::ptr addConditionTimer(uint64_t ms, const std::function<void()>& cb,
                                 const std::weak_ptr<void>& weak_cond, bool recurring = false);
    uint64_t getNextTimer(); // 到最近一个定时器执行的时间间隔
    void listExpiredCb(std::vector<std::function<void()>>& cbs); // 获取需要执行的定时器回调函数
    void tick(); // 执行定时器回调函数,清理超时定时器
    uint64_t  getNearestTime(); // 获取最近一个定时器执行的时间间隔
    bool hasTimer(); // 是否有定时器
protected:
//    virtual void onTimerInsertedAtFront() = 0; // 当有新的定时器插入到定时器首部时执行该函数
    void addTimer(const Timer::ptr& val, std::lock_guard<std::mutex>& lock); // 添加定时器
private:
    bool detectClockRollover(TimeStamp now); // 检测时钟是否回拨
private:
    std::set<Timer::ptr, Timer::Comparator> timers_; // 定时器集合
    bool tickled_ = false; // 是否触发onTimerInsertedAtFront
    TimeStamp previouse_time_; // 上次执行时间
    std::mutex mtx_;
};


#endif //SERVER_SERVER_TIMER_H
