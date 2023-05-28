#include "webserver.h"


WebServer::WebServer(){
    // http_conn类对象,创建MAX_FD个对象
    users = new http_conn[MAX_FD];

    // root文件夹路径
    char server_path[200];
    getcwd(server_path,200);
    char root[6] = "/root";
    m_root = (char *) malloc(strlen(server_path) + strlen(root) + 1);
    // std::cout<<strlen(server_path) + strlen(root) + 1<<endl;
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer(){
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(string user, string password, string databaseName,
              int port,             // 端口号
              int logwrite_mode,    // 日志写入方式
              int trig_mode,        // 触发组合模式(listenfd触发模式+connfd触发模式)
              int opt_linger,       // 优雅关闭链接 
              int sql_nums,         // 数据库连接池数量
              int thread_nums,      // 线程池内线程数量
              int closelog_flag,    // 是否关闭日志
              int actormodel       // 并发模型选择
              ){
    m_user = user;
    m_passWord = password;
    m_databaseName = databaseName;
    m_port = port;
    m_logwrite_mode = logwrite_mode;
    m_trig_mode = trig_mode;
    m_opt_linger = opt_linger;
    m_sql_nums = sql_nums;
    m_thread_nums = thread_nums;
    m_closelog_flag = closelog_flag;
    m_actor_model = actormodel;
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_trig_mode)
    {
        m_listentrig_mode = 0;
        m_conntrig_mode = 0;
    }
    //LT + ET
    else if (1 == m_trig_mode)
    {
        m_listentrig_mode = 0;
        m_conntrig_mode = 1;
    }
    //ET + LT
    else if (2 == m_trig_mode)
    {
        m_listentrig_mode = 1;
        m_conntrig_mode = 0;
    }
    //ET + ET
    else if (3 == m_trig_mode)
    {
        m_listentrig_mode = 1;
        m_conntrig_mode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_closelog_flag)
    {
        //初始化日志
        if (1 == m_logwrite_mode)
            Log::get_instance()->init("./ServerLog", m_closelog_flag, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_closelog_flag, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_nums, m_closelog_flag);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actor_model, m_connPool, m_thread_nums);
}

void WebServer::eventListen(){
    // 网络编程基础步骤
    // 
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //优雅关闭连接
    if (0 == m_opt_linger)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_opt_linger)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    // 将listenfd放在epoll树上
    utils.addfd(m_epollfd, m_listenfd, false, m_listentrig_mode);
    // 将上述epollfd赋值给http类对象的m_epollfd属性
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_conntrig_mode, m_closelog_flag, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

// 处理新到的客户连接
bool WebServer::dealclinetdata(){
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    // LT水平触发
    if(0 == m_listentrig_mode){
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if(connfd < 0){
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if(http_conn::m_user_count >= MAX_FD){
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    // ET非阻塞边缘触发
    else{
        while(1){
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if(connfd < 0){
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if(http_conn::m_user_count >= MAX_FD){
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

// 处理信号
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server){
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if(ret == -1) return false;
    else if (ret == 0) return false;
    else{
        for(int i = 0; i < ret; ++i){
            switch (signals[i]){
                case SIGALRM:{
                    timeout = true;
                    break;
                }
                case SIGTERM:{
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

// 处理客户连接上接收到的数据
void WebServer::dealwithread(int sockfd){
    util_timer *timer = users_timer[sockfd].timer;

    // reactor
    if(1 == m_actor_model){
        if (timer) adjust_timer(timer);

        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while(true){
            if(1 == users[sockfd].improv){
                if (1 == users[sockfd].timer_flag){
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else{
        // proactor
        if (users[sockfd].read_once()) {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if(timer) adjust_timer(timer);

        }
        else deal_timer(timer, sockfd);
    }
}

// 写入数据
void WebServer::dealwithwrite(int sockfd){
    util_timer *timer = users_timer[sockfd].timer;
    // reactor
    if(1 == m_actor_model){
        if(timer) adjust_timer(timer);
        m_pool->append(users + sockfd, 1);
        while(true){
            if (1 == users[sockfd].improv){
                if (1 == users[sockfd].timer_flag){
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else{
        //proactor
        if (users[sockfd].write()){
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            if (timer) adjust_timer(timer);
        }
        else deal_timer(timer, sockfd);

    }
}

// 服务器接收http请求
void WebServer::eventLoop(){
    bool timeout = false;
    bool stop_server = false;

    while(!stop_server){
        // 等待所监控文件描述符上有事件发生
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR){
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        // 对所有就绪事件进行处理
        for(int i = 0; i < number; i++){
            int sockfd = events[i].data.fd;

            // 处理新到的客户连接
            if(sockfd == m_listenfd){
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            // 处理异常事件
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号
            else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)){
                bool flag = dealwithsignal(timeout, stop_server);
                if(false == flag) LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理客户连接上接收到的数据
            else if(events[i].events & EPOLLIN){
                dealwithread(sockfd);
            }
            // 写入数据
            else if(events[i].events & EPOLLOUT){
                dealwithwrite(sockfd);
            }
        }
        if(timeout){
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}