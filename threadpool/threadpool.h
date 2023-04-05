#ifndef THREDAPOOL_H
#define THREDAPOOL_H

// stdio.h
#include <cstdio>
// 请求队列
#include <list>
// 异常
#include <exception>
// 线程
#include <pthread.h>
// 用于互斥锁
#include "../lock/locker.h"
// 数据库
#include "../CGImysql/sql_connection_pool.h"

template<typename T>
class threadpool{
public:
    // actor_model是并发模型选择
    // thread_number是线程池中线程的数量
    // max_requests是请求队列中最多允许、等待处理的请求数量
    // connPool是数据库连接池指针
    threadpool(int actor_model, connection_pool *connPool, int thread_numbers = 8, int max_requests=10000);
    ~threadpool();

    // 向请求队列插入任务请求，根据并发模型选择读事件格式
    // reactor
    bool append(T *request, int state);
    // proactor（默认）
    bool append_p(T *request);

private:
    // 工作线程运行的函数
    // 它不断从工作队列中取出任务并执行
    static void *worker(void *arg);
    // 执行work的任务
    void run();

private:
    int m_thread_numbers;       // 线程池中的线程数
    pthread_t *m_threads;       // 描述线程池的数组，其大小为线程数
    int m_max_requests;         // 请求队列中允许的最大请求数
    std::list<T *> m_workqueue; // 请求队列  
    locker m_queue_locker;      // 保护请求队列的互斥锁      
    sem m_queue_stat;           // 是否有任务需要处理
    connection_pool *m_connPool;// 数据库
    int m_actor_model;          // 模型切换
};

// 构造函数
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_numbers, int max_requests)\
    :m_actor_model(actor_model), m_thread_numbers(thread_numbers), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool){
    // 线程数目或最大请求数小于0
    if(m_thread_numbers <=0 || m_max_requests<=0) throw std::exception();

    // 线程ID初始化，Thread identifiers
    m_threads = new pthread_t[m_thread_numbers];
    // 创建失败
    if(!m_threads) throw std::exception();

    // 循环线程
    for(int i=0; i<m_thread_numbers; i++){
        // 循环创建线程,并将工作线程按要求运行
        // this传递类对象给静态函数worker
        if(pthread_create(m_threads+i, NULL, worker, this) != 0){
            delete[] m_threads;
            throw std::exception();
        }
        // 将线程进行分离，不用单独对工作线程进行回收
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 析构函数
template <typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;
} 

// 向请求队列中添加任务
template <typename T>
bool threadpool<T>::append(T *request, int state){
    // 互斥锁
    m_queue_locker.lock();

    // 根据硬件，预先设置请求队列的最大值
    if(m_workqueue.size() >= m_max_requests){
        m_queue_locker.unlock();
        return false;
    }
    // 状态信息
    request->m_state = state;

    // 添加任务
    m_workqueue.push_back(request);
    m_queue_locker.unlock();

    // 信号量提醒有任务要处理
    m_queue_stat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request){
    // 互斥锁
    m_queue_locker.lock();

    // 根据硬件先设置最大请求队列值
    if(m_workqueue.size() >= m_max_requests){
        m_queue_locker.unlock();
        return false;
    }
    // 添加任务
    m_workqueue.push_back(request);
    m_queue_locker.unlock();

    // 信号量提醒有任务要处理
    m_queue_stat.post();

    return true;
}

// 线程处理函数
template <typename T>
void *threadpool<T>::worker(void *arg){
    // 将类对象转换为线程池类
    threadpool *pool = (threadpool *) arg;
    // 调用私有成员函数run
    pool->run();
    return pool;
}

// run执行任务
template <typename T>
void threadpool<T>::run(){
    while(true){
        // 信号量等待
        m_queue_stat.wait();

        // 被唤醒后先加互斥锁
        m_queue_locker.lock();
        if(m_workqueue.empty()){
            m_queue_locker.unlock();
            continue;
        }

        // 从请求队列中取出第一个任务
        T* request = m_workqueue.front();
        // 将任务从请求队列删除
        m_workqueue.pop_front();
        m_queue_locker.unlock();
        if(!request) continue;

        if(1==m_actor_model){
            if(0==request->m_state){
                // http连接有两个标志位：improv和timer_flag
                // 标志位作用：Reactor模式下，当子线程执行读写任务出错时，通知主线程关闭子线程的客户连接
                // 主线程负责监听读写事件，线程池中线程负责IO操作
                if(request->read_once()){
                    request->improv = 1;
                    // 构造函数中*SQL = connPool->GetConnection();从连接池中取出一个数据库连接
                    // 析构函数中poolRAII->ReleaseConnection(conRAII);即将数据库连接放回连接池
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    // process(模板类中的方法，这里是http类)进行处理
                    request->process();
                }
                else {
                    // improv作用：保持主线程和子线程同步，置1表示http连接读写任务已完成，请求已处理完毕
                    request->improv = 1;
                    // timer_flag：标志子线程读写任务是否成功，失败为1
                    request->timer_flag = 1;
                }
            }
            else{
                if(request->write()) request->improv = 1;
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else{
            // 构造函数中*SQL = connPool->GetConnection();从连接池中取出一个数据库连接
            // 析构函数中poolRAII->ReleaseConnection(conRAII);即将数据库连接放回连接池
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            // process(模板类中的方法，这里是http类)进行处理
            request->process();
        }
    }
}
#endif
