# include "timer.h"


Timer::Timer(uint64_t ms, TimeoutCallBack cb, bool recurring, TimerManger *manager) {
    recurring_ = recurring;
    manager_ = manager;
    cb_ = std::move(cb);
    ms_ = ms;
    expires_ = Clock::now() + MS(ms);
}

Timer::Timer(TimeStamp expires) {
    expires_ = expires;
}

bool Timer::cancel() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (cb_) {
        cb_ = nullptr;
        auto ret = manager_->timers_.find(shared_from_this());
        if (ret != manager_->timers_.end()) {
            manager_->timers_.erase(ret);
            return true;
        }
    }
    return false;
}

bool Timer::refresh() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!cb_) {
        return false;
    }
    auto ret = manager_->timers_.find(shared_from_this());
    if (ret == manager_->timers_.end()) {
        return false;
    }
    manager_->timers_.erase(ret);
    expires_ = Clock::now() + MS(ms_);

    // todo: 是否需要调用addTimer
    manager_->timers_.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now) {
    if (ms == ms_ && !from_now) {
        return true;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    if (!cb_) {
        return false;
    }
    auto ret = manager_->timers_.find(shared_from_this());
    if (ret == manager_->timers_.end()) {
        return false;
    }
    manager_->timers_.erase(ret);
    /**
     * 从当前时间或者上一次执行时间开始计算，计算出定时器的新的执行时间。
     * 如果是从当前时间开始计算，那么获取当前时间；否则，使用定时器原来的
     * 下一次执行时间减去定时器的执行间隔得到执行时间的起始时间。
     */
    TimeStamp now = Clock::now();
    if (from_now) {
        expires_ = now + MS(ms);
    }
    ms_ = ms;
    // todo: 是否需要传入参数lock
    manager_->addTimer(shared_from_this(), lock);
    return true;
}

Timer::ptr TimerManger::addTimer(uint64_t ms, const TimeoutCallBack &cb, bool recurring) {
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    std::lock_guard<std::mutex> lock(mtx_);
    addTimer(timer, lock);
    return timer;
}

void TimerManger::addTimer(const Timer::ptr& timer, std::lock_guard<std::mutex> &lock) {
    auto it = timers_.insert(timer).first;
    bool at_front = (it == timers_.begin()) && !tickled_;
    if (at_front) {
        tickled_ = true;
//        onTimerInsertedAtFront();
    }
//    lock.unlock();
//    if (at_front) {
//        onTimerInsertedAtFront();
//    }
}


static void OnTimer(const std::weak_ptr<void>& weak_cond, std::function<void()> cb) {
    std::shared_ptr<void> tmp = weak_cond.lock();
    if (tmp) {
        cb();
    }
}

Timer::ptr TimerManger::addConditionTimer(uint64_t ms, const TimeoutCallBack &cb,
                                          const std::weak_ptr<void>& weak_cond,
                                          bool recurring) {
    // return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
    return addTimer(ms, [weak_cond, cb] { return OnTimer(weak_cond, cb); }, recurring);
}

uint64_t TimerManger::getNextTimer() {
    std::lock_guard<std::mutex> lock(mtx_);
    tickled_ = false;
    if (timers_.empty()) {
        return ~0ull;
    }
    const Timer::ptr& next = *timers_.begin();
    TimeStamp now = Clock::now();
    if (now >= next->expires_) {
        return 0;
    } else {
        return std::chrono::duration_cast<MS>(next->expires_ - now).count();
    }
}

void TimerManger::listExpiredCb(std::vector<std::function<void()>>& cbs) {
    TimeStamp now = Clock::now();
    std::vector<Timer::ptr> expired;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (timers_.empty()) {
            return;
        }
    }
    std::lock_guard<std::mutex> lock(mtx_);
    if (timers_.empty()) {
        return;
    }
    bool rollover = detectClockRollover(now);
    if (!rollover && (*timers_.begin())->expires_ > now) {
        return;
    }
    Timer::ptr now_timer(new Timer(now));
    auto it = rollover ? timers_.end() : timers_.lower_bound(now_timer);
    while (it != timers_.end() && (*it)->expires_ == now) {
        ++it;
    }
    expired.insert(expired.begin(), timers_.begin(), it);
    timers_.erase(timers_.begin(), it);
    cbs.reserve(expired.size());
    for (auto& timer : expired) {
        cbs.push_back(timer->cb_);
        if (timer->recurring_) {
            timer->expires_ = now + MS(timer->ms_);
            timers_.insert(timer);
        } else {
            timer->cb_ = nullptr;
        }
    }
}

bool TimerManger::hasTimer() {
    std::lock_guard<std::mutex> lock(mtx_);
    return !timers_.empty();
}

bool TimerManger::detectClockRollover(TimeStamp now) {
    bool rollover = false;
    if (now < previouse_time_ && now < (previouse_time_ - MS(60 * 60 * 1000 * 2))) {
        rollover = true;
    }
    previouse_time_ = now;
    return rollover;
}

void TimerManger::tick() {
    std::vector<std::function<void()>> cbs;
    listExpiredCb(cbs);
    std::vector<std::function<void()>>::iterator it;
    for (it = cbs.begin(); it != cbs.end(); ++it) {
        (*it)();
    }
}

uint64_t TimerManger::getNearestTime() {
    tick();
    uint64_t res = -1;
    if(!timers_.empty()) {
        res = std::chrono::duration_cast<MS>((*timers_.begin())->expires_ - Clock::now()).count();
        if(res < 0) {
            res = 0;
        }
    }
    return res;
}

bool Timer::Comparator::operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const {
    if(!lhs && !rhs) {
        return false;
    }
    if(!lhs) {
        return true;
    }
    if(!rhs) {
        return false;
    }
    // 升序
    if(lhs->expires_ < rhs->expires_) {
        return true;
    }
    if(rhs->expires_ < lhs->expires_) {
        return false;
    }
    // 如果两个定时器触发时间相同，则比较它们在内存中的地址，以保证排序的稳定性。
    return lhs.get() < rhs.get();
}
