//
// Created by leo on 23-6-3.
//

#ifndef SERVER_SQL_CONNECTION_POOL_H
#define SERVER_SQL_CONNECTION_POOL_H

#include <cstdio>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <cstring>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/server_log.h"
#include "../utils/server_utils.h"

class SqlConnectionPool {
public:
    SqlConnectionPool();
    ~SqlConnectionPool();

    MYSQL *GetConnection();				 //获取数据库连接
    bool ReleaseConnection(MYSQL *conn); //释放连接
    unsigned int getFreeConnection() const {
        return free_connection_;
    }

    unsigned int getMaxConnection() const {
        return max_connection_;
    }

    unsigned int getCurConnection() const {
        return cur_connection_;
    }

    void DestroyPool();					 //销毁所有连接

    //单例模式
    static SqlConnectionPool *GetInstance();

    void init(std::string config_file_path);

private:
    unsigned int max_connection_;  //最大连接数
    unsigned int cur_connection_;  //当前已使用的连接数
    unsigned int free_connection_; //当前空闲的连接数

private:
    Locker locker_;
    std::list<MYSQL *> connection_list_; //连接池
    Semaphore semaphore_;

    std::string host_; //主机地址
    std::string port_;		 //数据库端口号
    std::string user_name_;		 //登陆数据库用户名
    std::string password_;	 //登陆数据库密码
    std::string database_name_; //使用数据库名
    ServerUtils *server_utils_ = new ServerUtils();
};

class ConnectionRAII {
public:
    ConnectionRAII(MYSQL **SQL, SqlConnectionPool *connPool);
    ~ConnectionRAII();
private:
    MYSQL *connection_RAII_;
    SqlConnectionPool *connection_pool_RAII_;

};


#endif //SERVER_SQL_CONNECTION_POOL_H
