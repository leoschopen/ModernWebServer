//
// Created by leo on 23-6-2.
//

#ifndef SERVER_SERVER_TIMER_H
#define SERVER_SERVER_TIMER_H

#include <netinet/in.h>
#include <time.h>

#include "../log/server_log.h"

class Timer;
struct ClientData {
    sockaddr_in address;
    int sockfd;
    Timer *timer;
};

// 双向链表
class Timer {
public:
    Timer(): prev(nullptr), next(nullptr) {}
    ~Timer() = default;

    Timer(time_t expire, void (*cbFunc)(ClientData *), ClientData *clientData) : expire(expire), cb_func(cbFunc),
                                                                                 client_data(clientData),
                                                                                 prev(nullptr), next(nullptr) {}

public:
    time_t expire;
    void (*cb_func)(ClientData *);
    ClientData *client_data;
    Timer *prev;
    Timer *next;
};

class SortedTimerList {
public:
    SortedTimerList(): head(nullptr), tail(nullptr) {}
    ~SortedTimerList();

    // 将timer添加到链表中
    void add_timer(Timer *timer);
    // 调整timer在链表中的位置
    void adjust_timer(Timer *timer);
    void del_timer(Timer *timer);

    void tick();

private:
    // 将timer添加到list_head之后的链表中
    void add_timer(Timer *timer, Timer *list_head);

private:
    Timer *head;
    Timer *tail;
};


#endif //SERVER_SERVER_TIMER_H
