#ifndef CONFIG_H
#define CONFIG_H

#include "webserver.h"


using namespace std;

class Config{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc, char*argv[]);

    // 端口号
    int PORT;

    // 日志写入方式
    int LogWrite_mode;

    // 触发组合模式
    int Trig_mode;

    // listenfd触发模式
    int ListenTrig_mode;

    // connfd触发模式
    int ConnTrig_mode;

    // 优雅关闭链接
    int OPT_linger;

    // 数据库连接池数量
    int SQL_nums;

    // 线程池内线程数量
    int Thread_nums;

    // 是否关闭日志
    int CloseLog_flag;

    // 并发模型选择
    int ActorModel;

};

#endif