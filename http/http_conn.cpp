#include "http_conn.h"

#include <mysql/mysql.h>

#include <fstream>
#include <map>

#include "../log/log.h"

// #define connfdET //边缘触发非阻塞
#define connfdLT  // 水平触发阻塞

// #define listenfdET //边缘触发非阻塞
#define listenfdLT  // 水平触发阻塞

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

// 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
const char *doc_root = "/home/leo/webserver/TinyWebServer/root";

// 将表中的用户名和密码放入map
map<string, string> users;
locker m_lock;

void http_conn::initmysql_result(connection_pool *connPool) {
    // 先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    // 在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(
            mysql,
            "SELECT username,passwd FROM user"))  // 成功返回0，失败返回非0值
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot) {
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
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev) {
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

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;
    // int reuse=1;
    // setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init() {
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;  // 默认不保持连接
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/**
 * 在Windows系统下，回车式的换行符号是'\r\n';
 * 在Linux系统下是没有'\r';这里也不考虑
 * Mac系统里，每行结尾是“<回车>”，即“\r”。这里不考虑
 * 因此在解析文本或其他格式的文件内容时，碰到判定回车式换行的地方，就需要注意
 * 既要判定'\r\n'又要判定'\n'。
 */

// 从状态机，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN，分别表示读取到一个完整的行，行出错和行数据尚且不完整
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) ==
                m_read_idx)  // 说明'\r'是最后一个字符就已经结束了
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                // q:为什么要将\r\n改为\0\0
                // a:因为我们要将每一行的内容取出来，而每一行的内容都是以\0结尾的，所以我们要将\r\n改为\0\0
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 主线程读取浏览器端发来的全部数据，存储到m_read_buf中
// 循环读取客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    int bytes_read = 0;

#ifdef connfdLT

    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                      READ_BUFFER_SIZE - m_read_idx, 0);

    if (bytes_read <= 0) {
        return false;
    }

    m_read_idx += bytes_read;

    return true;

#endif

#ifdef connfdET
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                          READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)  // 表示读取数据出错
        {
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK)  // 表示数据已经读取完毕
                break;
            return false;
        } else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
#endif
}

// 解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    m_url = strpbrk(
        text,
        " \t");  // 将字符串text中首次出现的strCharSet中的任一字符字符c搜索出来，然后返回该字符c在字符串text中的位置
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else
        return BAD_REQUEST;
    m_url += strspn(m_url,
                    " \t");  // 跳过空格和\t，strspn函数返回指定字符串后面的下标
    m_version = strpbrk(m_url, " \t");
    if (!m_version) return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') return BAD_REQUEST;
    // 当url为/时，显示判断界面
    if (strlen(m_url) == 1) strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部信息
// 返回NO_REQUEST、GET_REQUEST、BAD_REQUEST
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    if (text[0] == '\0') {
        // 如果遇到空行，说明我们得到了一个正确的http请求
        if (m_content_length != 0) {
            // 将主状态机转移到CHECK_STATE_CONTENT状态
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        // printf("oop!unknow header: %s\n",text);
        LOG_INFO("oop!unknow header: %s", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机，解析请求,他的三个状态就是解析请求行，解析头部字段，解析请求内容
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // 这里为什么要写两个判断条件？第一个判断条件为什么这样写？
    /**
     * 1. m_check_state == CHECK_STATE_CONTENT
     * 表示解析到了请求体,这里采用POST请求，所以请求体不为空
     *    此外如果已经完成了请求体的解析，那么状态就会变成LINE_OPEN，从而能够跳出循环
     * 2.
     * 第二个条件表示解析到了一个完整的行，每一行的解析结束之后都会返回LINE_OK
     */
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();

        // m_start_line是每一个数据行在m_read_buf中的起始位置
        // m_checked_idx表示从状态机在m_read_buf中读取的位置
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        Log::get_instance()->flush();

        // 主状态机的三种状态转移逻辑
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                // 完整解析POST请求后，跳转到报文响应函数
                if (ret == GET_REQUEST) return do_request();
                // 解析完消息体即完成报文解析，避免再次进入循环，更新line_status
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

/**
 * 该函数将网站根目录和url文件拼接，然后通过stat判断该文件属性并返回相应的HTTP_CODE
 * 另外，为了提高访问速度，通过mmap进行映射，将普通文件映射到内存逻辑地址。
 */
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    printf("m_url:%s\n", m_url);  //  /xxx.jpg
    printf("m_real_file:%s\n",
           m_real_file);  //  /home/leo/webserver/TinyWebServer/root
    printf("doc_root:%s\n",
           doc_root);  //  /home/leo/webserver/TinyWebServer/root

    const char *p = strrchr(m_url, '/');  // 从右向左查找字符出现的位置

    // 处理cgi
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
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);  // /CGISQL.cgi

        // 将src复制到dest后面，返回dest，确保最终复制n个字符
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        // new_m_real_file:/home/leo/webserver/TinyWebServer/root/CGISQL.cgi
        free(m_url_real);

        // 将用户名和密码提取出来
        // m_string:user=admin%40qq.com&password=123456789
        // user=123 & passwd=123

        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i) name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        // 同步线程登录校验
        if (*(p + 1) == '3') {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            } else
                strcpy(m_url, "/registerError.html");
        }
        // 如果是登录，直接判断
        // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2') {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    if (*(p + 1) == '0') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '1') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '5') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '6') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else if (*(p + 1) == '7') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    // 失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(m_real_file, &m_file_stat) < 0) return NO_RESOURCE;

    //判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;

    //判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;

    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中，将一个文件映射到进程的地址空间中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address =
        (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    
    //避免文件描述符的浪费和占用
    close(fd);

    //表示请求文件存在，且可以访问
    return FILE_REQUEST;
}

void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//服务器子线程调用process_write完成响应报文，随后注册epollout事件。
//服务器主线程检测写事件，并调用http_conn::write函数将响应报文发送给浏览器端。
// 注意，这里和讲解的代码不一样，为什么要修改？
// 书中原代码的write函数不严谨，讲解代码中对其中的Bug进行了修复，可以正常传输大文件。
bool http_conn::write() {
    int temp = 0;

    //若要发送的数据长度为0，则表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1) {
        //将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        temp = writev(m_sockfd, m_iv, m_iv_count);

        //返回-1表示发送出错，返回0表示发送缓冲区已满，下次继续发送
        if (temp < 0) {
            //如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件。
            //虽然在此期间，服务器无法立即接收到同一客户的下一个请求，但这可以保证连接的完整性
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        //第一个iovec头部信息的数据已发送完，发送第二个iovec数据
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            //继续发送第一个iovec头部信息的数据
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        //判断条件，数据已全部发送完
        if (bytes_to_send <= 0) {
            unmap();
            //在epoll树上重置EPOLLONESHOT事件
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            //浏览器的请求为长连接
            if (m_linger) {
                //重新初始化HTTP对象
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

// 更新m_write_idx指针和缓冲区m_write_buf中的内容。
bool http_conn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) return false;
    va_list arg_list;
    va_start(arg_list, format);

    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx,
                        WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    
    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);//释放资源,清空可变参数列表
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    LOG_INFO("request:%s", m_write_buf);
    Log::get_instance()->flush();
    return true;
}
bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n",
                        (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line() { return add_response("%s", "\r\n"); }
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

//根据do_request的返回状态，服务器子线程调用process_write向m_write_buf中写入响应报文。
bool http_conn::process_write(HTTP_CODE ret) {
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
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;

                //第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;

                //发送的全部数据为响应报文头部信息和文件大小
                bytes_to_send = m_write_idx + m_file_stat.st_size;
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
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
void http_conn::process() {
    HTTP_CODE read_ret = process_read();  // 解析请求
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
