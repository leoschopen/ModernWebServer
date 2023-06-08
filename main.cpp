#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cassert>

#include "./CGImysql/sql_connection_pool.h"
#include "./http/http_connection.h"
#include "./threadpool/threadpool.h"
#include "./timer/server_timer.h"

#define MAX_FD 65536            // 最大文件描述符
#define MAX_EVENT_NUMBER 10000  // 最大事件数
#define TIMESLOT 5              // 最小超时单位

#define SYNLOG  // 同步写日志
// #define ASYNLOG //异步写日志

// #define listenfdET //边缘触发非阻塞
#define listenfdLT  // 水平触发阻塞

// 这三个函数在http_conn.cpp中定义，改变链接属性
extern int add_fd(int epollfd, int fd, bool one_shot);
extern int remove_fd(int epollfd, int fd);
extern int setnonblocking(int fd);

// 设置定时器相关参数
static int pipefd[2];
static SortedTimerList timer_lst;
static int epollfd = 0;

// 信号处理函数
void sig_handler(int sig) {
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号函数
void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler() {
    timer_lst.tick();//遍历定时器列表，检查每个定时器是否到期，并执行相应的事件处理函数。
    alarm(TIMESLOT);//向操作系统注册一个定时器，指定定时器的时间间隔为TIMESLOT秒,他会在指定的时间间隔后向进程发送一个SIGALRM信号
}

// 定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(ClientData * user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    HttpConnection::user_count_--;
    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
}

void show_error(int connfd, const char* info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char* argv[]) {
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8);  // 异步日志模型
#endif

#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0);  // 同步日志模型
#endif

    if (argc <= 1) {
        printf("usage: %s ./server port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi(argv[1]);

    addsig(SIGPIPE, SIG_IGN);  // 忽略SIGPIPE信号

    // 创建数据库连接池
    SqlConnectionPool* connPool = SqlConnectionPool::GetInstance();
    connPool->init("/home/leo/webserver/ModernWebserver2/utils/config.ini");

    // 创建线程池
    ThreadPool<HttpConnection>* pool = NULL;
    try {
        pool = new ThreadPool<HttpConnection>(connPool);
    } catch (...) {
        return 1;
    }

    HttpConnection* users = new HttpConnection[MAX_FD];
    assert(users);

    // 初始化数据库读取表
    users->init_mysql_result(connPool);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    // struct linger tmp={1,0};
    // SO_LINGER若有数据待发送，延迟关闭
    // setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    /* SO_REUSEADDR 允许端口被重复使用 */
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));

    // 打印listenfd的ip和端口号
//    struct sockaddr_in addr;
//    socklen_t len = sizeof(addr);
//    getsockname(listenfd, (struct sockaddr*)&addr, &len);
//    char ip[20];
//    printf("ip=%s, port=%d\n", inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)), ntohs(addr.sin_port));
//
//    return 0;

    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);

    add_fd(epollfd, listenfd, false);
    HttpConnection::epollfd_ = epollfd;

    // 创建管道,注册pipefd[0]上的可读事件
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    // 管道写端写入信号值，管道读端通过I/O复用系统监测读事件
    setnonblocking(pipefd[1]);
    add_fd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    bool stop_server = false;

    ClientData* users_timer = new ClientData[MAX_FD];

    bool timeout = false;
    alarm(TIMESLOT);

    while (!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
#ifdef listenfdLT
                int connfd = accept(listenfd, (struct sockaddr*)&client_address,
                                    &client_addrlength);
                if (connfd < 0) {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                if (HttpConnection::user_count_ >= MAX_FD) {
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, client_address);

                // 初始化client_data数据
                // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                Timer* timer = new Timer;
                timer->client_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_lst.add_timer(timer);
#endif

#ifdef listenfdET
                while (1) {
                    int connfd =
                        accept(listenfd, (struct sockaddr*)&client_address,
                               &client_addrlength);
                    if (connfd < 0) {
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                        break;
                    }
                    if (HttpConnection::m_user_count >= MAX_FD) {
                        show_error(connfd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }
                    users[connfd].init(connfd, client_address);

                    // 初始化client_data数据
                    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                    users_timer[connfd].address = client_address;
                    users_timer[connfd].sockfd = connfd;
                    util_timer* timer = new util_timer;
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_lst.add_timer(timer);
                }
                continue;
#endif
            }
            // 发生三种情况，1. 对端断开连接或半关闭连接，2. 文件描述符被挂起或被关闭事件，3. 发生错误 的任意一种，则处理
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 服务器端关闭连接，移除对应的定时器
                Timer* timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);

                if (timer) {
                    timer_lst.del_timer(timer);
                }
            }

            // 有定时器信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) {
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for (int i = 0; i < ret; ++i) {
                        switch (signals[i]) {
                            case SIGALRM: {
                                timeout = true;
                                break;
                            }
                            case SIGTERM: {
                                stop_server = true;
                            }
                        }
                    }
                }
            }

            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN) {
                Timer* timer = users_timer[sockfd].timer;
                if (users[sockfd].read_once()) {
                    LOG_INFO("deal with the client(%s)",
                             inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    // 若监测到读事件，将该事件放入请求队列
                    pool->append(users + sockfd);

                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 并对新的定时器在链表上的位置进行调整
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                } else {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                }
            } else if (events[i].events & EPOLLOUT) {
                Timer* timer = users_timer[sockfd].timer;
                if (users[sockfd].write()) {
                    LOG_INFO("send data to the client(%s)",
                             inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 并对新的定时器在链表上的位置进行调整
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                } else {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        if (timeout) {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    return 0;
}
