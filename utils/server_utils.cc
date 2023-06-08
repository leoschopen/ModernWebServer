//
// Created by leo on 23-6-2.
//

#include "server_utils.h"

void ServerUtils::add_sig(int sig, void (*handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;

    // 确保系统调用被信号处理函数中断之后能够自动重启
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);

    // 为信号sig安装信号处理函数,加入内核维护的信号处理函数链表中
    assert(sigaction(sig, &sa, nullptr) != -1);
}

void ServerUtils::show_error(int connfd, const char *info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

void ServerUtils::sig_handler(int sig, int *pipe_fd) {
    // 为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipe_fd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

std::map<std::string, std::string> ServerUtils::read_config(const std::string &filename) {
    std::map<std::string, std::string> config;
    std::ifstream in(filename);
    if (!in) {
        LOG_ERROR("%s%s", "config file not found", filename.c_str());
        throw std::runtime_error("config file not found: " + filename);
    }
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
        } else {
            auto pos = line.find('=');
            if (pos != std::string::npos) {
                auto key = strip(line.substr(0, pos));
                auto value = strip(line.substr(pos + 1));
                config[section + '.' + key] = value;
            }
        }
    }
    return config;
}

std::string ServerUtils::get_config(const std::string &key,
                                    const std::map<std::string, std::string> &config) {
    auto it = config.find(key);
    if (it == config.end()) {
        throw std::runtime_error("config key not found: " + key);
    }
    return it->second;
}

std::string ServerUtils::strip(const std::string &str, const std::string &chars){
    auto first = str.find_first_not_of(chars);
    auto last = str.find_last_not_of(chars);
    if (first == std::string::npos || last == std::string::npos) {
        return "";
    }
    return str.substr(first, last - first + 1);
}


const std::string &ServerUtils::getServerRoot() const {
    return server_root_;
}

const std::string &ServerUtils::getPort() const {
    return port_;
}

const std::string &ServerUtils::getUser() const {
    return user_;
}

const std::string &ServerUtils::getPasswd() const {
    return passwd_;
}

const std::string &ServerUtils::getDatabase() const {
    return database_;
}

const std::string &ServerUtils::getMaxConnection() const {
    return max_connection_;
}

const std::string &ServerUtils::getHost() const {
    return host_;
}

const std::string &ServerUtils::getConnfdType() const {
    return connfd_type_;
}

const std::string &ServerUtils::getListenfdType() const {
    return listenfd_type_;
}

ServerUtils::ServerUtils(std::string config_file_path)
                :config_file_path_(std::move(config_file_path)){
    auto config = read_config(config_file_path_);
    server_root_ = get_config("server.root", config);
    connfd_type_ = get_config("socket.connfd", config);
    listenfd_type_ = get_config("socket.listenfd", config);
    host_ = get_config("database.host", config);
    port_ = get_config("database.port", config);
    user_ = get_config("database.user", config);
    passwd_ = get_config("database.password", config);
    database_ = get_config("database.name", config);
    max_connection_ = get_config("database.max_connection", config);
}

ServerUtils::ServerUtils() {
    config_file_path_ = "/home/leo/webserver/ModernWebserver/utils/config.ini";
    auto config = read_config(config_file_path_);
    server_root_ = get_config("server.root", config);
    connfd_type_ = get_config("socket.connfd", config);
    listenfd_type_ = get_config("socket.listenfd", config);
    host_ = get_config("database.host", config);
    port_ = get_config("database.port", config);
    user_ = get_config("database.user", config);
    passwd_ = get_config("database.password", config);
    database_ = get_config("database.name", config);
    max_connection_ = get_config("database.max_connection", config);
}

















