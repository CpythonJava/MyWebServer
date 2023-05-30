#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;
// 初始化数据库读取表
void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

// ******epoll事件******
// ******epoll事件******
// 对文件描述符设置非阻塞
int setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
// 针对客户端连接的描述符,listenfd不开启
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;

    if(1 == TRIGMode) event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else event.events = EPOLLIN | EPOLLRDHUP;

    if(one_shot)  event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;

    if(1 == TRIGMode) event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_closelog_flag = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    // 初始化byte_to_send
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
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
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机，读取报文的一行，用于分析出一行内容
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
// m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节
// m_checked_idx指向从状态机当前正在分析的字节
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        // temp为将要分析的字节
        // 从状态机从m_read_buf中逐字节读取
        // 从状态机在m_read_buf中位于m_checked_idx位置读取数据
        temp = m_read_buf[m_checked_idx];
        // 如果当前是\r字符，则有可能会读取到完整行
        if (temp == '\r')
        {
            // 下一个字符达到了buffer结尾，则接收不完整，需要继续接收
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            // 接下来的字符是\n，将\r\n修改成\0\0
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                // 改\r
                m_read_buf[m_checked_idx++] = '\0';
                // 改\n，将m_checked_idx指向下一行的开头
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 如果都不符合，则返回语法错误
            return LINE_BAD;
        }
        // 如果当前字符是\n，也有可能读取到完整行
        // 一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if (temp == '\n')
        {
            // 前一个字符是\r，则接收完整
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                // 改\r
                m_read_buf[m_checked_idx - 1] = '\0';
                // 改\n，将m_checked_idx指向下一行的开头
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 当前字节既不是\r，也不是\n
    // 表示接收不完整，需要继续接收，返回LINE_OPEN
    return LINE_OPEN;
}

// 循环读取浏览器端发来的客户数据，直到无数据可读或对方关闭连接
// 非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once(){
    if(m_read_idx >= READ_BUFFER_SIZE) return false;
    // 读取字节数
    int bytes_read = 0;

    // LT读取数据
    if(0 == m_TRIGMode){
        // 从套接字接收数据,存储在m_read_buf缓冲区
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        // 修改m_read_idx读取字节数
        m_read_idx += bytes_read;
        if(bytes_read <= 0) return false;
        return true;
    }
    // ET读数据
    else{
        while(true){
            // 从套接字接收数据,存储在m_read_buf缓冲区
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if(bytes_read == -1){
                // 非阻塞ET模式下,需要一次性读完数据
                if(errno == EAGAIN || errno == EWOULDBLOCK) break;
                return false;
            }
            else if(bytes_read == 0) return false;
            // 修改m_read_idx读取字节数
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 在HTTP报文中，请求行中各个部分之间通过\t或空格分隔。
    // 请求行中最先含有空格和\t任一字符的位置并返回
    // 在字符串中查找第一个是"\t"或者空格的位置
    m_url = strpbrk(text, " \t");
    // 如果没有空格或\t，则报文格式有误
    if (!m_url)
        return BAD_REQUEST;
    // 将该位置改为\0，用于将前面数据取出
    *m_url++ = '\0';
    // 取出数据，并通过与GET和POST比较，以确定请求方式
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    // m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    // 将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    m_url += strspn(m_url, " \t");

    // 判断HTTP版本号
    m_version = strpbrk(m_url, " \t");
    // 如果没有空格或\t，则报文格式有误
    if (!m_version)
        return BAD_REQUEST;
    // 将该位置改为\0，用于将前面数据取出
    *m_version++ = '\0';
    // 取出数据，并通过与http和https比较，以确定版本
    m_version += strspn(m_version, " \t");
    // 仅支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    // 对请求资源前7个字符进行判断http
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    // 对请求资源前8个字符进行判断https
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    // 一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    // 当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    // 请求行处理完毕，将主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // 判断是空行还是请求头
    // 空行的话
    if (text[0] == '\0')
    {
        // 判断是GET还是POST请求
        // 如果m_content_length不为0，则为POST
        if (m_content_length != 0)
        {
            // 状态转移到CHECK_STATE_CONTENT
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 如果是GET，报文解析结束
        return GET_REQUEST;
    }
    // 解析请求头部连接字段
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        // 跳过空格和\t字符
        text += strspn(text, " \t");
        // 判断是keep-alive还是close，决定是长连接还是短连接
        if (strcasecmp(text, "keep-alive") == 0)
        {
            // 如果是长连接，则将m_linger保存长连接标志位
            m_linger = true;
        }
    }
    // 解析请求头部内容长度字段
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        // 跳过空格和\t字符
        text += strspn(text, " \t");
        // m_content_length保存读取post请求的消息体长度
        m_content_length = atol(text);
    }
    // 解析请求头部HOST字段
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        // 跳过空格和\t字符
        text += strspn(text, " \t");
        // m_host保存请求头部HOST字段
        m_host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    // 判断buffer中是否读取了消息体
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 报文解析
http_conn::HTTP_CODE http_conn::process_read()
{
    // 初始化从状态机状态、HTTP请求解析结果
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    // 判断GET请求(每一行都是\r\n作为结束) : (line_status = parse_line()) == LINE_OK 
    // 判断POST请求(消息体的末尾没有任何字符) : m_check_state==CHECK_STATE_CONTENT
    while ((m_check_state == CHECK_STATE_CONTENT    // 判断POST请求(消息体的末尾没有任何字符)
    && line_status == LINE_OK)                      // POST解析后主状态机还是CHECK_STATE_CONTENT,需要补充从状态机的状态进行条件判断
    || ((line_status = parse_line()) == LINE_OK))   // 判断GET请求(每一行都是\r\n作为结束)
    {
        // 从状态机读取数据间接赋给text
        text = get_line();
        // m_start_line是每一个数据行在m_read_buf中的起始位置
        // m_checked_idx表示从状态机在m_read_buf中读取的位置
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);

        //  主状态机的三种状态转移逻辑
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            // 解析请求行
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            // 解析请求头部
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            // 完整解析GET请求后，跳转到报文响应函数
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            // 解析body
            ret = parse_content(text);
            // 完整解析POST请求后，跳转到报文响应函数
            if (ret == GET_REQUEST)
                return do_request();
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

// 生成响应报文
http_conn::HTTP_CODE http_conn::do_request()
{
    // 网站根目录，文件夹内存放请求的资源和跳转的html文件
    // 将初始化的m_real_file赋值为网站根目录
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // TODO print url
    // printf("m_url:%s\n", m_url);
    // 找到m_url中/的位置
    const char *p = strrchr(m_url, '/');

    // 实现登录和注册校验
    // cgi标志位为1,打开CGI登录校验
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        // POST请求，进行注册校验
        if (*(p + 1) == '3')
        {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                    // 注册成功跳转到log.html，即登录页面
                    strcpy(m_url, "/log.html");
                else
                    // 注册失败跳转到registerError.html，即注册失败页面
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        // POST请求，进行登录校验
        // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {   
            // TODO
            // 同步线程登录校验
            // CGI多进程登录校验
            if (users.find(name) != users.end() && users[name] == password)
                // 验证成功跳转到welcome.html，即资源请求成功页面
                strcpy(m_url, "/welcome.html");
            else
                // 验证失败跳转到logError.html，即登录失败页面
                strcpy(m_url, "/logError.html");
        }
    }
    // POST请求，跳转到register.html，即注册页面
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        // 将网站目录和/register.html进行拼接，更新到m_real_file中
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // POST请求，跳转到log.html，即登录页面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        // 将网站目录和/log.html进行拼接，更新到m_real_file中
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // POST请求，跳转到picture.html，即图片请求页面
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // POST请求，跳转到video.html，即视频请求页面
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // POST请求，跳转到fans.html，即关注页面
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        //如果以上均不符合，直接将url与网站目录拼接
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // 对目标文件的属性作分析
    // 通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    if (stat(m_real_file, &m_file_stat) < 0)
        // 失败返回NO_RESOURCE状态，表示资源不存在
        return NO_RESOURCE;

    // m_file_stat.st_mode为文件类型和权限
    // 判断文件的权限，是否可读
    if (!(m_file_stat.st_mode & S_IROTH))
        // 不可读则返回FORBIDDEN_REQUEST状态
        return FORBIDDEN_REQUEST;

    // 判断文件类型
    if (S_ISDIR(m_file_stat.st_mode))
        // 如果是目录，则返回BAD_REQUEST，表示请求报文有误
        return BAD_REQUEST;
    
    // 以只读方式获取文件描述符
    int fd = open(m_real_file, O_RDONLY);

    // m_file_stat.st_size为文件大小(字节数)
    // 使用mmap将文件映射到内存地址m_file_address处,提高文件访问速度
    // 0:由系统决定映射区的起始地址
    // m_file_stat.st_size:映射区的长度为文件长度
    // PROT_READ:表示页内容可以被读取
    // MAP_PRIVATE:建立一个写入时拷贝的私有映射，内存区域的写入不会影响到原文件
    // fd：文件描述符
    // 0:被映射对象内容的起点
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    
    // 避免文件描述符的浪费和占用
    close(fd);

    // 表示请求文件获取成功，且可以访问
    return FILE_REQUEST;
}

// 取消mmap映射
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 将响应信息和请求文件聚集写到TCP Socket本身定义的发送缓冲区，内核发给用户
bool http_conn::write()
{
    int temp = 0;
    // 若要发送的数据长度为0
    // 表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        // 将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        // m_sockfd：文件描述符
        // m_iv：所有iovec
        // m_iv_count：iovec数目
        // temp为发送的字节数
        temp = writev(m_sockfd, m_iv, m_iv_count);

        // 发送失败
        if (temp < 0)
        {
            // 如果缓冲区已满
            if (errno == EAGAIN)
            {
                // 重新注册写事件
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }

            // 如果缓冲区未满
            // 取消mmap映射
            unmap();
            // 关闭连接
            return false;
        }

        // 发送成功
        // 更新已发送字节
        bytes_have_send += temp;
        // 更新待发送字节，偏移文件iovec的指针
        bytes_to_send -= temp;
        // 第一个iovec头部信息的数据已发送完，发送第二个iovec数据
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            // 不再继续发送头部信息
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        // 继续发送第一个iovec头部信息的数据
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        // 判断条件，数据已全部发送完
        if (bytes_to_send <= 0)
        {
            // 取消mmap映射
            unmap();
            // 在epoll树上重置EPOLLONESHOT事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
            // 浏览器的请求为长连接
            if (m_linger)
            {
                // 重置http类实例，重新初始化HTTP对象，注册读事件，不关闭连接
                init();
                return true;
            }
            else
            {
                // 直接关闭连接
                return false;
            }
        }
    }
}

// 更新m_write_idx指针和缓冲区m_write_buf中的内容
bool http_conn::add_response(const char *format, ...)
{
    // 如果写入内容超出m_write_buf大小则报错
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    // 定义可变参数列表
    va_list arg_list;
    // 将变量arg_list初始化为传入参数
    va_start(arg_list, format);
    // 将数据format从可变参数列表写入缓冲区，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    // 如果写入的数据长度超过缓冲区剩余空间，则报错
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    // 更新m_write_idx位置
    m_write_idx += len;
    // 清空可变参列表
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}

// 添加状态行
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加消息报头，具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}

// 添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

// 添加文本类型，这里是html
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

// 添加空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

// 添加文本content
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

// 向m_write_buf中写入响应报文
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    // 内部错误，500
    case INTERNAL_ERROR:
    {
        // 将状态行写入connfd的写缓存
        add_status_line(500, error_500_title);
        // 将响应头写入connfd的写缓存
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    // 报文语法有误，404
    case BAD_REQUEST:
    {
        // 将状态行写入connfd的写缓存
        add_status_line(404, error_404_title);
        // 将响应头写入connfd的写缓存
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    // 资源没有访问权限，403
    case FORBIDDEN_REQUEST:
    {
        // 将状态行写入connfd的写缓存
        add_status_line(403, error_403_title);
        // 将响应头写入connfd的写缓存
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    // 文件存在，200
    case FILE_REQUEST:
    {
        // 将状态行写入connfd的写缓存
        add_status_line(200, ok_200_title);
        // 如果请求的资源存在
        if (m_file_stat.st_size != 0)
        {
            // 将响应头写入connfd的写缓存
            add_headers(m_file_stat.st_size);
            // 第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            // 第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            // iovec数目
            m_iv_count = 2;
            // 初始化byte_to_send，包括响应报文头部信息和文件数据大小
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            // 如果请求的资源大小为0，则返回空白html文件
            const char *ok_string = "<html><body></body></html>";
            // 将响应头写入connfd的写缓存
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    // 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 各子线程通过process函数对任务进行处理
void http_conn::process(){
    // 对读入connfd读缓冲区的请求报文解析
    HTTP_CODE read_ret = process_read();
    // NO_REQUEST，表示请求不完整，需要继续接收请求数据
    if(read_ret == NO_REQUEST){
        // 注册并监听读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    // 报文响应
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        // 关闭连接
        close_conn();
    }
    // 将connfd修改位EPOLLOUT事件
    // 子线程注册写事件，主线程监听写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
