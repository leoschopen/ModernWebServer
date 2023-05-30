#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../CGImysql/sql_connection_pool.h"
#include "../lock/locker.h"
class http_conn {
   public:
    static const int FILENAME_LEN = 200; // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048; // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区的大小
    enum METHOD { // HTTP请求方法，但我们仅支持GET
        GET = 0, // 从服务器获取资源
        POST,   // 向服务器提交数据
        HEAD,  // 获得报文首部
        PUT,  // 向服务器添加文档
        DELETE, // 从服务器删除文档
        TRACE, // 对经过的路径进行追踪
        OPTIONS, // 显示服务器支持的请求方法
        CONNECT, // 用于代理进行传输报文
        PATH   // 用于将请求转发给某个内部服务器
    };
    enum CHECK_STATE { // 主状态机的可能状态
        CHECK_STATE_REQUESTLINE = 0, // 当前正在分析请求行
        CHECK_STATE_HEADER,         // 当前正在分析头部字段
        CHECK_STATE_CONTENT        // 当前正在分析请求内容
    };
    enum HTTP_CODE { // 服务器处理HTTP请求的可能结果
        NO_REQUEST,  // 请求不完整，需要继续读取客户数据
        GET_REQUEST, // 获得了一个完整的客户请求
        BAD_REQUEST, // 客户请求有语法错误
        NO_RESOURCE, // 没有资源
        FORBIDDEN_REQUEST, // 客户对资源没有足够的访问权限
        FILE_REQUEST,     // 客户请求的资源可以正常访问
        INTERNAL_ERROR,  // 服务器内部错误
        CLOSED_CONNECTION // 客户端已经关闭连接了
    };
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN }; // 从状态机的可能状态, LINE_OK表示读取到一个完整的行，LINE_BAD表示行出错，LINE_OPEN表示行数据尚且不完整

   public:
    http_conn() {}
    ~http_conn() {}

   public:
    void init(int sockfd, const sockaddr_in &addr); 
    void close_conn(bool real_close = true);
    void process(); // 处理客户请求
    bool read_once(); // 非阻塞读操作
    bool write(); // 非阻塞写操作
    sockaddr_in *get_address() { return &m_address; }
    void initmysql_result(connection_pool *connPool); // 初始化数据库读取表

   private:
    void init(); // 初始化连接
    HTTP_CODE process_read(); // 解析HTTP请求
    bool process_write(HTTP_CODE ret); // 填充HTTP应答
    HTTP_CODE parse_request_line(char *text); // 解析请求行
    HTTP_CODE parse_headers(char *text); // 解析请求头
    HTTP_CODE parse_content(char *text); // 解析请求内容
    HTTP_CODE do_request(); // 处理客户请求
    char *get_line() { return m_read_buf + m_start_line; }; // 从状态机读取一行，分析是请求行还是头部字段，编译器的优化，隐式内联
    LINE_STATUS parse_line(); // 分析一行的内容
    void unmap(); // 释放资源
    bool add_response(const char *format, ...); // 向写缓冲区写入待发送的数据
    bool add_content(const char *content); // 向写缓冲区写入待发送的数据
    bool add_status_line(int status, const char *title); // 添加状态行 
    bool add_headers(int content_length); // 添加消息报头
    bool add_content_type(); // 添加消息报头
    bool add_content_length(int content_length); // 添加消息报头
    bool add_linger(); // 添加消息报头
    bool add_blank_line(); // 添加空行

   public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;

   private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;  // 缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    int m_checked_idx;  // 已经解析的字符个数
    int m_start_line;   // 当前正在解析的行的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;  // 写缓冲区中待发送的字节数
    CHECK_STATE m_check_state; // 主状态机当前所处的状态
    METHOD m_method; // 请求方法
    char m_real_file[FILENAME_LEN]; // 客户请求的目标文件的完整路径，其内容等于doc_root + m_url, doc_root是网站根目录
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count; // 被写内存块的数量
    int cgi;         // 是否启用的POST
    char *m_string;  // 存储请求头数据
    int bytes_to_send;
    int bytes_have_send;
};

#endif
