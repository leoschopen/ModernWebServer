//
// Created by leo on 23-6-2.
//

#ifndef SERVER_SERVER_UTILS_H
#define SERVER_SERVER_UTILS_H

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


class ServerUtils{
public:
    ServerUtils();

    explicit ServerUtils(std::string config_file_path);

    ~ServerUtils() = default;

    static void add_sig(int sig, void(handler)(int), bool restart = true);
    static void show_error(int connfd, const char* info);
    static void sig_handler(int sig, int *pipe_fd);

private:
    std::map<std::string, std::string> read_config(const std::string& filename);
    std::string get_config(const std::string& key,
                                  const std::map<std::string, std::string>& config);
    std::string strip(const std::string& str, const std::string& chars = " \t\n\r");

public:
    const std::string &getServerRoot() const;

    const std::string &getPort() const;

    const std::string &getUser() const;

    const std::string &getPasswd() const;

    const std::string &getDatabase() const;

    const std::string &getMaxConnection() const;

    const std::string &getHost() const;

    const std::string &getConnfdType() const;

    const std::string &getListenfdType() const;



private:
    // server的配置信息
    std::string config_file_path_;
    std::string connfd_type_;
    std::string listenfd_type_;
    // 数据库的配置信息
    std::string server_root_;
    std::string host_;
    std::string port_;
    std::string user_;
    std::string passwd_;
    std::string database_;
    std::string max_connection_;
};


#endif //SERVER_SERVER_UTILS_H