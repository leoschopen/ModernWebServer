//
// Created by leo on 23-6-2.
//

#include "server_timer.h"

SortedTimerList::~SortedTimerList() {
    Timer *tmp = head;
    while (tmp) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void SortedTimerList::add_timer(Timer *timer) {
    if(!timer) return;
    if(!head) {
        head = tail = timer;
        return;
    }
    if(timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

void SortedTimerList::del_timer(Timer *timer) {
    if (!timer) {
        return;
    }
    if ((timer == head) && (timer == tail)) {
        delete timer;
        head = nullptr;
        tail = nullptr;
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        delete timer;
        return;
    }
    if (timer == tail) {
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void SortedTimerList::adjust_timer(Timer *timer) {
    if(!timer) return;
    Timer *tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire)) {
        return;
    }
    if (timer == head){
        head = head -> next;
        head->prev = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
    }else{
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void SortedTimerList::tick() {
    if(!head) return;
    LOG_INFO("%s", "timer tick");
    Log::get_instance()->flush();
    Timer *tmp = head;
    time_t cur = time(nullptr);
    while(tmp){
        if(cur<tmp->expire) break;
        tmp->cb_func(tmp->client_data);
        head = tmp->next;
        if(head) head->prev = nullptr;
        delete tmp;
        tmp = head;
    }
}

void SortedTimerList::add_timer(Timer *timer, Timer *list_head) {
    Timer *prev = list_head;
    Timer *tmp = prev->next;
    while (tmp) {
        if (timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            return;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp) {
        prev ->next = timer;
        timer->prev = prev;
        timer->next = nullptr;
        tail = timer;
    }
}

