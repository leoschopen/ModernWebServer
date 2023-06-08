//
// Created by leo on 23-6-3.
//

#include "sql_connection_pool.h"

SqlConnectionPool::SqlConnectionPool() {
    cur_connection_ = 0;
    free_connection_ = 0;
}



void SqlConnectionPool::init(std::string config_file_path) {
    // 获取数据库配置项
    std::string host = server_utils_->getHost();
    std::string port = server_utils_->getPort();
    std::string name = server_utils_->getDatabase();
    std::string user = server_utils_->getUser();
    std::string password = server_utils_->getPasswd();
    std::string max_connection = server_utils_->getMaxConnection();
    int _max_connection = std::stoi(max_connection);

    host_ = host;
    user_name_ = user;
    password_ = password;
    database_name_ = name;
    port_ = port;

    locker_.lock();
    for(int i=0;i<_max_connection;i++){
        MYSQL *connection = nullptr;
        connection = mysql_init(connection);
        if(connection == nullptr){
            LOG_ERROR("%s","MYSQL CONNECTION ERROR: ",mysql_error(connection));
            exit(1);
        }
        connection = mysql_real_connect(connection,host_.c_str(),
                                        user_name_.c_str(),password_.c_str(),
                                        database_name_.c_str(),std::stoi(port_),
                                        nullptr,0);
        if(connection == nullptr){
            LOG_ERROR("%s","MYSQL CONNECTION ERROR: ",mysql_error(connection));
            exit(1);
        }
        connection_list_.push_back(connection);
        ++free_connection_;
    }
    semaphore_ = Semaphore(free_connection_);
    max_connection_ = free_connection_;
    locker_.unlock();
}

MYSQL *SqlConnectionPool::GetConnection() {
    MYSQL *connection = nullptr;
    if(connection_list_.empty()){
        return nullptr;
    }
    semaphore_.wait();
    locker_.lock();
    connection = connection_list_.front();
    connection_list_.pop_front();
    --free_connection_;
    ++cur_connection_;
    locker_.unlock();
    return connection;
}

bool SqlConnectionPool::ReleaseConnection(MYSQL *conn) {
    if (conn == nullptr) {
        return false;
    }
    locker_.lock();
    connection_list_.push_back(conn);
    ++free_connection_;
    --cur_connection_;
    locker_.unlock();
    semaphore_.post();
    return true;
}

void SqlConnectionPool::DestroyPool() {
    locker_.lock();
    if(!connection_list_.empty()){
        std::list<MYSQL *>::iterator it;
        for(it = connection_list_.begin();it!=connection_list_.end();++it){
            MYSQL *connection = *it;
            mysql_close(connection);
        }
        cur_connection_ = 0;
        free_connection_ = 0;
        connection_list_.clear();
        locker_.unlock();
    }
    locker_.unlock();
}

SqlConnectionPool *SqlConnectionPool::GetInstance() {
    static SqlConnectionPool connection_pool;
    return &connection_pool;
}

SqlConnectionPool::~SqlConnectionPool() {
    DestroyPool();
}


ConnectionRAII::ConnectionRAII(MYSQL **conn, SqlConnectionPool *connPool) {
    *conn = connPool->GetConnection();
    connection_RAII_ = *conn;
    connection_pool_RAII_ = connPool;
}

ConnectionRAII::~ConnectionRAII() {
    connection_pool_RAII_->ReleaseConnection(connection_RAII_);
}
