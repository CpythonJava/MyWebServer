#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

// sockaddr_in:为套接字安排地址
#include <netinet/in.h>
#include <map>
// fcntl:根据文件描述符来操作文件的特性
#include <fcntl.h>
// epoll_event：内核事件
#include <sys/epoll.h>
// stat：状态
#include <sys/stat.h>
// mmap
#include <sys/mman.h>
// close
#include <unistd.h>
// writev
#include <sys/uio.h>

#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"


class http_conn{
public:
    // 设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
    // 设置缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
    //设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // 报文请求方法
    enum METHOD{
        GET=0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATH
    };
    // 主状态机的状态
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT
    };
    // 报文解析的结果
    enum HTTP_CODE{
        NO_REQUEST, GET_REQUEST, BAD_REQUEST,
        NO_RESOURCE,FORBIDDEN_REQUEST,
        FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION
    };
    // 从状态机的状态
    enum LINE_STATUS{
        LINE_OK = 0, LINE_BAD, LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化套接字地址,函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    // 关闭http连接
    void close_conn(bool real_close = true);
    // 处理
    void process();
    // 读取浏览器端发来的全部数据
    bool read_once();
    // 响应报文写入函数
    bool write();
    sockaddr_in *get_address(){return &m_address;} 
    // 同步线程初始化数据库读取表
    void initmysql_result(connection_pool *connPool);
    // 保持主线程和子线程同步，置1表示http连接读写任务已完成
    int improv;
    // 标志子线程读写任务是否成功，失败为1
    int timer_flag;


private:
    // 函数内部会调用私有方法init
    void init();
    // 从m_read_buf读取,并处理请求报文
    HTTP_CODE process_read();
    // 向m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);
    // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    // 主状态机解析报文中的请求内容数据
    HTTP_CODE parse_content(char *text);
    // 生成响应报文
    HTTP_CODE do_request();
    
    // 将指针向后偏移，指向未处理的字符
    // m_start_line是行在buffer中的起始位置，将该位置后面的数据赋给text
    // 此时从状态机已提前将一行的末尾字符\r\n变为\0\0
    // 所以text可以直接取出完整的行进行解析
    char *get_line(){
        return m_read_buf + m_start_line;
    };
    // 从状态机读取一行,分析是请求报文的哪一个部分
    LINE_STATUS parse_line();

    void unmap();
    // 响应8个部分
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    //读为0, 写为1
    int m_state;  

private:
    int m_sockfd;
    sockaddr_in m_address;
    // 存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
    // 缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    long m_read_idx;
    // m_read_buf读取的位置m_checked_idx
    long m_checked_idx;
     // m_read_buf中已经解析的字符个数
    int m_start_line;

    // 存储发出的响应报文数据
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 指示buffer中的长度
    int m_write_idx;

    CHECK_STATE m_check_state;
    METHOD m_method;
    // 解析请求报文中对应的6个变量
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    long m_content_length;
    bool m_linger;

    // 读取服务器上的文件地址
    char *m_file_address;
    struct stat m_file_stat;
    // io向量机制iovec
    struct iovec m_iv[2];
    int m_iv_count;
    // 是否启用的POST
    int cgi; 
    // 存储请求头数据      
    char *m_string; 
    // 剩余发送字节数
    int bytes_to_send;
    // 已发送字节数
    int bytes_have_send;
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_closelog_flag;

    // 数据库用户名
    char sql_user[100];
    // 数据库用户密码
    char sql_passwd[100];
    // 数据库名称
    char sql_name[100];
};

#endif
