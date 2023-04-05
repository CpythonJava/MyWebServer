#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <cassert>

// getopt函数
#include <unistd.h>
// atoi函数
#include <stdlib.h>
// epoll函数
#include <sys/epoll.h>

// TODO：1.http
#include "./http/http_conn.h"
// TODO：6.threadpool
#include "./threadpool/threadpool.h"

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //最小超时单位

class WebServer{
public:
    WebServer();
    ~WebServer();

    void init(string user, string password, string databaseName,
              int port,             // 端口号
              int logwrite_mode,    // 日志写入方式
              int trig_mode,        // 触发组合模式(listenfd触发模式+connfd触发模式)
              int opt_linger,       // 优雅关闭链接 
              int sql_nums,         // 数据库连接池数量
              int thread_nums,      // 线程池内线程数量
              int closelog_flag,    // 是否关闭日志
              int actormodel       // 并发模型选择
              ); 
    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    // 基础
    // 数据库相关
    connection_pool *m_connPool;
    string m_user;              //登陆数据库用户名
    string m_passWord;          //登陆数据库密码
    string m_databaseName;      //使用数据库名
    int m_sql_nums;             // 数据库连接池数量

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_nums;          // 线程池内线程数量

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    char *m_root;               // root文件夹地址

    int m_port;                 // 端口号
    int m_logwrite_mode;        // 日志写入方式
    int m_trig_mode;            // 触发组合模式(listenfd触发模式+connfd触发模式)
    int m_opt_linger;           // 优雅关闭链接
    int m_listenfd;
    int m_listentrig_mode;      // listenfd触发模式
    int m_conntrig_mode;        // connfd触发模式
    int m_closelog_flag;        // 是否关闭日志
    int m_actormodel;           // 并发模型选择

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    //定时器相关
    client_data *users_timer;
    Utils utils;

};

#endif