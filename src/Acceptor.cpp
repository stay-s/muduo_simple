#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

// 创建非阻塞的监听 socket
// SOCK_NONBLOCK：非阻塞模式（accept/read/write 不会卡住线程）
// SOCK_CLOEXEC：exec 时自动关闭此 fd（防止子进程继承）
static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if(sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}


Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblocking())         // 创建监听 socket
    , acceptChannel_(loop, acceptSocket_.fd())   // 为 listenfd 创建 Channel
    , listenning_(false)
{
    acceptSocket_.setReuseAddr(true);    // 允许重用 TIME_WAIT 地址
    acceptSocket_.setReusePort(true);    // 允许多进程绑定同一端口
    acceptSocket_.bindAddress(listenAddr); // 绑定地址

    // 设置 listenfd 的读回调：有新连接到来时调用 handleRead
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    // 取消所有事件监听并从 Poller 中移除
    acceptChannel_.disableAll();
    acceptChannel_.remove();
}

// 开始监听：底层调用 listen() 并注册读事件到 epoll
void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen();            // 调用 ::listen() 系统调用
    acceptChannel_.enableReading();    // 注册到 epoll，开始监听可读事件
}

// listenfd 可读时的回调：说明有新客户端连接到来
void Acceptor::handleRead()
{
    InetAddress peerAddr;
    // accept4：接受新连接并返回新的 connfd
    // 新 fd 自动设为非阻塞（SOCK_NONBLOCK）
    int connfd = acceptSocket_.accept(&peerAddr);
    if(connfd >= 0)
    {
        // 如果设置了新连接回调（TcpServer::newConnection），则调用它
        if(newConnectionCallback_)
        {
            newConnectionCallback_(connfd, peerAddr);
        }
        else
        {
            // 没有设置回调，说明没人处理这个连接，直接关闭
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d accept err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
        // EMFILE：当前进程打开的文件描述符数已达到上限
        if(errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reached limit\n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}
