//
// Created by leo on 23-6-7.
//

#ifndef SERVER_SOCKET_UTILS_H
#define SERVER_SOCKET_UTILS_H

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <stdio.h>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <stdexcept>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include "../log/server_log.h"

namespace socketutils{
    inline void sys_err(const char *str)
    {
        perror(str);
        exit(1);
    }
    inline void perr_exit(const char *str)
    {
        perror(str);
        exit(-1);
    }
    inline void show_error(int connfd, const char* info) {
        printf("%s", info);
        send(connfd, info, strlen(info), 0);
        close(connfd);
    }

    inline int Socket(int domain, int type, int protocol){
        int fd = socket(domain, type, protocol);
        if(fd == -1){
            perr_exit("socket error");
            return fd;
        }
        return fd;
    }

    inline int Listen(int sockfd, int backlog){
        int listen_success = listen(sockfd, backlog);
        if(listen_success == -1){
            sys_err("listen error");
            return listen_success;
        }
        return listen_success;
    }

    inline int Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
        int bind_success = bind(sockfd, addr, addrlen);
        if(bind_success == -1){
            sys_err("bind error");
            return bind_success;
        }
        return bind_success;
    }

    inline int Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen){
        int n;
        again:
        if((n=accept(sockfd, addr, addrlen)) < 0){
            if((errno == ECONNABORTED) || (errno == EINTR))
                goto again;
            else{
                sys_err("accept error");
                return n;
            }
        }
        return n;
    }

    inline int Connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
        int connect_success = connect(sockfd, addr, addrlen);
        if(connect_success == -1){
            sys_err("connect error");
            return connect_success;
        }else if(connect_success == 0){
            printf("connect success\n");
        }
        return connect_success;
    }


    inline ssize_t Epoll_create(int size){
        ssize_t n;
        if((n = epoll_create(size)) == -1){
            perr_exit("epoll_create error");
        }
        return n;
    }

    inline ssize_t Epoll_ctl(int epfd, int op, int fd, struct epoll_event *event){
        ssize_t n;
        if((n = epoll_ctl(epfd, op, fd, event)) == -1){
            perr_exit("epoll_ctl error");
        }
        return n;
    }

    inline ssize_t Epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout){
        ssize_t n;
        if((n = epoll_wait(epfd, events, maxevents, timeout)) == -1){
            perr_exit("epoll_wait error");
        }
        return n;
    }


    inline void Addr_init(struct sockaddr_in *addr,int family, int ip,int port){
        bzero(addr, sizeof(*addr));
        addr->sin_family = family;
        addr->sin_port = htons(port);
        addr->sin_addr.s_addr = htonl(ip);
    }

    inline void PrintSockInfo(int fd){
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        getsockname(fd, (struct sockaddr*)&addr, &len);
        printf("ip = %s, port = %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    }

// 对文件描述符设置非阻塞
    inline int setnonblocking(int fd) {
        int old_option = fcntl(fd, F_GETFL);
        int new_option = old_option | O_NONBLOCK;
        fcntl(fd, F_SETFL, new_option);
        return old_option;
    }


// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    inline void add_fd(int epollfd, int fd, bool one_shot) {
        epoll_event event;
        event.data.fd = fd;

#ifdef connfdET
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef connfdLT
        event.events =
            EPOLLIN |
            EPOLLRDHUP;  // 添加EPOLLRDHUP事件，表示对方关闭连接，或者对方关闭了写操作
#endif

#ifdef listenfdET
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT
        event.events = EPOLLIN | EPOLLRDHUP;
#endif

        if (one_shot) event.events |= EPOLLONESHOT;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
        setnonblocking(fd);
    }

// 从内核时间表删除描述符
    inline void remove_fd(int epollfd, int fd) {
        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
        close(fd);
    }

// 将事件重置为EPOLLONESHOT
    inline void mod_fd(int epollfd, int fd, int ev) {
        epoll_event event;
        event.data.fd = fd;

#ifdef connfdET
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

        epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    }
}




#endif //SERVER_SOCKET_UTILS_H
