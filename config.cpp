#include "config.h"

Config::Config(){

    // 端口号,默认9006
    PORT = 9006;

    // 日志写入方式，默认同步
    LogWrite_mode = 0;

    // 触发组合模式，默认listenfd LT + connfd LT
    Trig_mode = 0;

    // listenfd触发模式，默认LT
    ListenTrig_mode = 0;

    // connfd触发模式，默认LT
    ConnTrig_mode = 0;

    // 优雅关闭链接，默认不使用
    OPT_linger = 0;

    // 数据库连接池数量,默认8
    SQL_nums = 8;

    // 线程池内线程数量,默认8
    Thread_nums = 8;

    // 是否关闭日志,默认不关闭
    CloseLog_flag = 0;

    // 并发模型选择,默认是proactor
    ActorModel = 0;


};





void Config::parse_arg(int argc, char*argv[]){
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    // -p，自定义端口号——默认——9006
    // -l，选择日志写入方式，默认同步写入——0
        // 0，同步写入
        // 1，异步写入
    // -m，listenfd和connfd的模式组合，默认使用LT + LT——0
        // 0，表示使用LT + LT
        // 1，表示使用LT + ET
        // 2，表示使用ET + LT
        // 3，表示使用ET + ET
    // -o，优雅关闭连接，默认不使用——0
        // 0，不使用
        // 1，使用
    // -s，数据库连接数量——默认——8
    // -t，线程数量——默认——8
    // -c，关闭日志，默认打开——0
        // 0，打开日志
        // 1，关闭日志
    // -a，选择反应堆模型，默认Proactor——0
        // 0，Proactor模型
        // 1，Reactor模型
    
    // getopt() 方法是用来分析命令行参数的
    // 该方法由 Unix 标准库提供，包含在 <unistd.h> 头文件中
    while ((opt = getopt(argc, argv, str)) != -1){
        switch (opt)
        {
        case 'p':{
            PORT = atoi(optarg);
            break;
        } 
        case 'l':{
            LogWrite_mode = atoi(optarg);
            break;
        } 
        case 'm':{
            Trig_mode = atoi(optarg);
            break;
        } 
        case 'o':{
            OPT_linger = atoi(optarg);
            break;
        } 
        case 's':{
            SQL_nums = atoi(optarg);
            break;
        } 
        case 't':{
            Thread_nums = atoi(optarg);
            break;
        } 
        case 'c':{
            CloseLog_flag = atoi(optarg);
            break;
        } 
        case 'a':{
            ActorModel = atoi(optarg);
            break;
        } 
        default:
            break;
        }
    }
}


    
//     {
//         switch (opt)
//         {
//         case 'p':
//         {
//             PORT = atoi(optarg);
//             break;
//         }
//         case 'l':
//         {
//             LOGWrite = atoi(optarg);
//             break;
//         }
//         case 'm':
//         {
//             TRIGMode = atoi(optarg);
//             break;
//         }
//         case 'o':
//         {
//             OPT_LINGER = atoi(optarg);
//             break;
//         }
//         case 's':
//         {
//             sql_num = atoi(optarg);
//             break;
//         }
//         case 't':
//         {
//             thread_num = atoi(optarg);
//             break;
//         }
//         case 'c':
//         {
//             close_log = atoi(optarg);
//             break;
//         }
//         case 'a':
//         {
//             actor_model = atoi(optarg);
//             break;
//         }
//         default:
//             break;
//         }
//     }
// }