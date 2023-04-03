#include "config.h"
#include <string>
// using namespace std;

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "1234";
    string databasename = "yourdb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);
    WebServer server;


    return 0;
}