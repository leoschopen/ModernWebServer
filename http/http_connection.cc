//
// Created by leo on 23-6-4.
//

#include "http_connection.h"

// #define connfdET //边缘触发非阻塞
#define connfdLT  // 水平触发阻塞

// #define listenfdET //边缘触发非阻塞
#define listenfdLT  // 水平触发阻塞

map<string, string> users_map_;
Locker locker_;

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form =
        "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form =
        "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form =
        "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form =
        "There was an unusual problem serving the request file.\n";


const char *doc_root = "/home/leo/webserver/ModernWebserver/root";


int HttpConnection::epollfd_ = -1;
int HttpConnection::user_count_ = 0;

//// 对文件描述符设置非阻塞
//int setnonblocking(int fd) {
//    int old_option = fcntl(fd, F_GETFL);
//    int new_option = old_option | O_NONBLOCK;
//    fcntl(fd, F_SETFL, new_option);
//    return old_option;
//}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void add_fd(int epollfd, int fd, bool one_shot) {
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
    socketutils::setnonblocking(fd);
}

// 从内核时间表删除描述符
void remove_fd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void mod_fd(int epollfd, int fd, int ev) {
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




void HttpConnection::init() {
    mysql_ = nullptr;
    bytes_to_send_ = 0;
    bytes_have_send_ = 0;
    check_state_ = CHECK_STATE_REQUESTLINE;
    linger_ = false;  // 默认不保持连接
    method_ = GET;
    url_ = 0; //-
    version_ = 0;//-
    content_length_ = 0;
    host_ = 0;//-
    start_line_ = 0;
    checked_idx_ = 0;
    read_idx_ = 0;
    write_idx_ = 0;
    cgi_ = 0;
    memset(read_buf_, '\0', kReadBufferSize);
    memset(write_buf_, '\0', kWriteBufferSize);
    memset(real_file_, '\0', kFilenameLen);
}


void HttpConnection::init(int sockfd, const sockaddr_in &addr) {
    sockfd_ = sockfd;
    address_ = addr;
    add_fd(epollfd_, sockfd_, true);
    ++user_count_;
    init();
}

void HttpConnection::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
         mod_fd(epollfd_, sockfd_, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
     mod_fd(epollfd_, sockfd_, EPOLLOUT);
}

// 从socket中读取数据到read_buf_
bool HttpConnection::read_once() {
    if (read_idx_ >= kReadBufferSize) return false;
    int bytes_read = 0;

#ifdef connfdLT
    bytes_read = recv(sockfd_, read_buf_ + read_idx_,
                      kReadBufferSize - read_idx_, 0);
    if (bytes_read <= 0) {
        return false;
    }
    read_idx_ += bytes_read;
    return true;
#endif

#ifdef connfdET
    for(;;){
        bytes_read = recv(sockfd_, read_buf_ + read_idx_,
                          kReadBufferSize - read_idx_, 0);
        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) break;
            return false;
        } else if (bytes_read == 0) {
            return false;
        }
        read_idx_ += bytes_read;
    }
    return true;
#endif
}

//服务器子线程调用process_write完成响应报文，随后注册epollout事件。
//服务器主线程检测写事件，并调用http_conn::write函数将响应报文发送给浏览器端。
bool HttpConnection::write() {
    int temp = 0;
    if (bytes_to_send_ == 0) {
        mod_fd(epollfd_, sockfd_, EPOLLIN);
        init();
        return true;
    }
    for (;;) {
        temp = writev(sockfd_, iv_, iv_count_);
        if (temp <= -1) {
            if (errno == EAGAIN) {
                mod_fd(epollfd_, sockfd_, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send_ += temp;
        bytes_to_send_ -= temp;

        if (bytes_have_send_ >= iv_[0].iov_len) {
            iv_[0].iov_len = 0;
            iv_[1].iov_base = file_address_ + (bytes_have_send_ - write_idx_);
            iv_[1].iov_len = bytes_to_send_;
        } else {
            iv_[0].iov_base = write_buf_ + bytes_have_send_;
            iv_[0].iov_len = iv_[0].iov_len - bytes_have_send_;
        }

        if (bytes_to_send_ <= 0) {
            unmap();
            mod_fd(epollfd_, sockfd_, EPOLLIN);
            if (linger_) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

void HttpConnection::init_mysql_result(SqlConnectionPool *connPool) {
    MYSQL *mysql = nullptr;
    ConnectionRAII mysql_conn(&mysql, connPool);

    if (mysql_query(mysql, "SELECT username, passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }
    MYSQL_RES *result = mysql_store_result(mysql);
    int num_fields = mysql_num_fields(result); //结果集中的列数
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        // std::cout<<"初始化数据库，用户名，密码："<< temp1 << " " << temp2 << std::endl;
        users_map_[temp1] = temp2;
    }
}

void HttpConnection::close_conn(bool real_close) {
    if (real_close && sockfd_ != -1) {
        remove_fd(epollfd_, sockfd_);
        sockfd_ = -1;
        --user_count_;
    }
}

// 不断读取行，在状态之间切换
HttpConnection::HTTP_CODE HttpConnection::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = nullptr;
    while ((check_state_ == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        start_line_ = checked_idx_;
        LOG_INFO("%s", text);
        switch (check_state_) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

bool HttpConnection::process_write(HttpConnection::HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) return false;
            break;
        }
        case BAD_REQUEST: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) return false;
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) return false;
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if (file_stat_.st_size != 0) {
                add_headers(file_stat_.st_size);
                //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
                iv_[0].iov_base = write_buf_;
                iv_[0].iov_len = write_idx_;

                //第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
                iv_[1].iov_base = file_address_;
                iv_[1].iov_len = file_stat_.st_size;
                iv_count_ = 2;

                //发送的全部数据为响应报文头部信息和文件大小
                bytes_to_send_ = write_idx_ + file_stat_.st_size;
                return true;
            } else {
                //如果请求的资源大小为0，则返回空白html文件
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)) return false;
            }
        }
        default:
            return false;
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    iv_[0].iov_base = write_buf_;
    iv_[0].iov_len = write_idx_;
    iv_count_ = 1;
    bytes_to_send_ = write_idx_;
    return true;
}

// GET https://www.baidu.com/content-search.xml HTTP/1.1
HttpConnection::HTTP_CODE HttpConnection::parse_request_line(char *text) {
    url_ = strpbrk(text, " \t");
    if (!url_) {
        return BAD_REQUEST;
    }
    *url_++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0) {
        method_ = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        method_ = POST;
        cgi_ = 1;
    } else {
        return BAD_REQUEST;
    }

    url_ += strspn(url_, " \t");
    version_ = strpbrk(url_, " \t");
    if (!version_) {
        return BAD_REQUEST;
    }
    *version_++ = '\0';
    version_ += strspn(version_, " \t");
    // 仅支持HTTP/1.1
    if (strcasecmp(version_, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    if (strncasecmp(url_, "http://", 7) == 0) {
        url_ += 7;
        url_ = strchr(url_, '/');
    }
    if (strncasecmp(url_, "https://", 8) == 0) {
        url_ += 8;
        url_ = strchr(url_, '/');
    }
    // 一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if (!url_ || url_[0] != '/') {
        return BAD_REQUEST;
    }
    // 当url为/时，显示欢迎界面
    if (strlen(url_) == 1) {
        strcat(url_, "judge.html");
    }
    check_state_ = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HttpConnection::HTTP_CODE HttpConnection::parse_headers(char *text) {
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0') {
        if (content_length_ != 0) {
            check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            linger_ = true;
        }
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        content_length_ = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        host_ = text;
    } else {
        LOG_INFO("oop!unknow header %s", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
HttpConnection::HTTP_CODE HttpConnection::parse_content(char *text) {
    // 判断buffer中是否读取了消息体
    if (read_idx_ >= (content_length_ + checked_idx_)) {
        text[content_length_] = '\0';
        request_header_content_ = text;
        return GET_REQUEST;
    }
    return HttpConnection::NO_REQUEST;
}

HttpConnection::LINE_STATUS HttpConnection::parse_line() {
    char temp;
    for (; checked_idx_ < read_idx_; ++checked_idx_) {
        temp = read_buf_[checked_idx_];
        if (temp == '\r') {
            if ((checked_idx_ + 1) == read_idx_) {
                return LINE_OPEN;
            } else if (read_buf_[checked_idx_ + 1] == '\n') {
                read_buf_[checked_idx_++] = '\0';
                read_buf_[checked_idx_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (checked_idx_ > 1 && read_buf_[checked_idx_ - 1] == '\r') {
                read_buf_[checked_idx_ - 1] = '\0';
                read_buf_[checked_idx_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/**
 * 该函数将网站根目录和url文件拼接，然后通过stat判断该文件属性并返回相应的HTTP_CODE
 * 另外，为了提高访问速度，通过mmap进行映射，将普通文件映射到内存逻辑地址。
 */
HttpConnection::HTTP_CODE HttpConnection::do_request() {
//    strcpy(real_file_,  server_utils_->getServerRoot().c_str());
    strcpy(real_file_,doc_root);
//    printf("m_url:%s\n", url_);  //  /xxx.jpg
//    printf("m_real_file:%s\n",
//           real_file_);  //  /home/leo/webserver/TinyWebServer/root
    int len =  strlen(doc_root);
    const char *p = strrchr(url_, '/');
    // 实现登录和注册校验
    /**
     * /2CGISQL.cgi
     *      POST请求，进行登录校验
     *      验证成功跳转到welcome.html，即资源请求成功页面
     *      验证失败跳转到logError.html，即登录失败页面
     * /3CGISQL.cgi
     *      POST请求，进行注册校验
     *      注册成功跳转到log.html，即登录页面
     *      注册失败跳转到registerError.html，即注册失败页面
     */
    if (cgi_ == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // 根据标志判断是登录检测还是注册检测
        char flag = url_[1];
        char *url_real = (char *) malloc(sizeof(char) * 200);
        strcpy(url_real, "/");
        strcat(url_real, url_ + 2);
        strncpy(real_file_ + len, url_real, kFilenameLen - len - 1);
        free(url_real);
        // 将参数user传递给mmap
        char name[100], password[100];
        int i;
        for (i = 5; request_header_content_[i] != '&'; ++i) {
            name[i - 5] = request_header_content_[i];
        }
        name[i - 5] = '\0';
        int j = 0;
        for (i = i + 10; request_header_content_[i] != '\0'; ++i, ++j) {
            password[j] = request_header_content_[i];
        }
        password[j] = '\0';

        // 注册，通过数据库验证用户名和密码
        if (*(p + 1) == '3') {
            char *sql_insert = (char *) malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, password) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            if (users_map_.find(name) != users_map_.end()) {
                locker_.lock();
                int res = mysql_query(mysql_, sql_insert);
                users_map_.insert(std::pair<string, string>(name, password));
                locker_.unlock();
                if (!res) strcpy(url_, "/log.html");
                else strcpy(url_, "/registerError.html");
            } else {
                strcpy(url_, "/registerError.html");
            }
        } else if (*(p + 1) == '2') {
            // 登录，通过数据库验证用户名和密码
            if (users_map_.find(name) != users_map_.end() && users_map_[name] == password) {
                strcpy(url_, "/welcome.html");
            } else {
                strcpy(url_, "/logError.html");
            }
        }
    }
    // 请求资源为/0hello.html
    if (*(p + 1) == '0') {
        char *url_real_ = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real_, "/register.html");
        strncpy(real_file_ + len, url_real_, strlen(url_real_));

        free(url_real_);
    } else if (*(p + 1) == '1') {
        char *url_real_ = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real_, "/log.html");
        strncpy(real_file_ + len, url_real_, strlen(url_real_));

        free(url_real_);
    } else if (*(p + 1) == '5') {
        char *url_real_ = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real_, "/picture.html");
        strncpy(real_file_ + len, url_real_, strlen(url_real_));

        free(url_real_);
    } else if (*(p + 1) == '6') {
        char *url_real_ = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real_, "/video.html");
        strncpy(real_file_ + len, url_real_, strlen(url_real_));

        free(url_real_);
    } else if (*(p + 1) == '7') {
        char *url_real_ = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real_, "/fans.html");
        strncpy(real_file_ + len, url_real_, strlen(url_real_));

        free(url_real_);
    } else
        strncpy(real_file_ + len, url_, kFilenameLen - len - 1);

    // 通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    // 失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(real_file_, &file_stat_) < 0) return NO_RESOURCE;

    //判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(file_stat_.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;

    //判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(file_stat_.st_mode)) return BAD_REQUEST;

    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中，将一个文件映射到进程的地址空间中
    int fd = open(real_file_, O_RDONLY);
    file_address_ =
            (char *)mmap(0, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    //避免文件描述符的浪费和占用
    close(fd);

    //表示请求文件存在，且可以访问
    return FILE_REQUEST;
}

void HttpConnection::unmap() {
    if(file_address_){
        munmap(file_address_,file_stat_.st_size);
        file_address_=nullptr;
    }
}

// 更新m_write_idx指针和缓冲区m_write_buf中的内容。
bool HttpConnection::add_response(const char *format, ...) {
    if (write_idx_ >= kWriteBufferSize) return false;
    va_list arg_list;
    va_start(arg_list, format);

    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(write_buf_ + write_idx_,
                        kWriteBufferSize - 1 - write_idx_, format, arg_list);

    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (kWriteBufferSize - 1 - write_idx_)) {
        va_end(arg_list);//释放资源,清空可变参数列表
        return false;
    }
    write_idx_ += len;
    va_end(arg_list);
    LOG_INFO("request:%s", write_buf_);
    Log::get_instance()->flush();
    return true;
}

bool HttpConnection::add_content(const char *content) {
    return add_response("%s", content);
}

bool HttpConnection::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConnection::add_headers(int content_length) {
    add_content_length(content_length);
    add_linger();
    add_blank_line();
}

bool HttpConnection::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool HttpConnection::add_content_length(int content_length) {
    return add_response("Content-Length:%d\r\n", content_length);
}

bool HttpConnection::add_linger() {
    return add_response("Connection:%s\r\n",
                        (linger_ == true) ? "keep-alive" : "close");
}

bool HttpConnection::add_blank_line() {
    return add_response("%s", "\r\n");
}



