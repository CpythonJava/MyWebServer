#include "config.h"

int main(int argc, char *argv[])
{
    // 需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "1234";
    string databasename = "yourdb";

    // 命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    // 初始化
    server.init(user, passwd, databasename, 
                config.PORT, config.LogWrite_mode, config.Trig_mode,
                config.OPT_linger, config.SQL_nums, config.Thread_nums, 
                config.CloseLog_flag, config.ActorModel);

    //日志
    server.log_write();

    // 数据库
    server.sql_pool();

    // 线程池
    server.thread_pool();

    // 触发模式
    server.trig_mode();

    // 监听
    server.eventListen();

    // 运行
    server.eventLoop();

    return 0;
}