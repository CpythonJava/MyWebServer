#include "webserver.h"


WebServer::WebServer(){
    // http_conn类对象
    users = new http_conn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    getcwd(server_path,200);
    char root[6] = "/root";
    m_root = (char *) malloc(strlen(server_path) + strlen(root) + 1);
    // std::cout<<strlen(server_path) + strlen(root) + 1<<endl;
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
    // 定时器


}