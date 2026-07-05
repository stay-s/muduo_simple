#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

#include <functional>
#include <string.h>

// 辅助函数：确保传入的 EventLoop 指针不为空
static EventLoop* CheckLoopNotNull(EventLoop* loop)
{
    if(loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

// TcpServer 构造函数：初始化所有组件
TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr, const std::string &nameArg, Option option)
    : loop_(CheckLoopNotNull(loop))
    , ipPort(listenAddr.toIpPort())
    , name_(nameArg)
    , acceptor_(new Acceptor(loop, listenAddr, option == kReusePort))
    , threadPool_(new EventLoopThreadPool(loop, name_))
    , connectionCallback_()
    , messageCallback_()
    , writeCompleteCallback_()
    , nextConnId_(1)
    , started_(0)
{
    // 当 Acceptor 接受到新连接时，回调 TcpServer::newConnection
    // Acceptor 负责 accept，TcpServer 负责创建 TcpConnection 并分配线程
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    // 销毁所有连接：通知每个 TcpConnection 执行 connectDestroyed
    for(auto& item : connections_)
    {
        TcpConnectionPtr conn(item.second);
        item.second.reset();  // 释放 TcpServer 持有的 shared_ptr
        // 在连接所属的 IO 线程中执行销毁操作（线程安全）
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    }
}

void TcpServer::setThreadNum(int numThreads)
{
    numThreads_ = numThreads;
    threadPool_->setThreadNum(numThreads_);
}

// 启动服务器（只启动一次，通过原子变量保证）
void TcpServer::start()
{
    // fetch_add 返回旧值，只有第一次调用时旧值为 0
    if(started_.fetch_add(1) == 0)
    {
        // 启动 IO 线程池
        threadPool_->start(threadInitCallback_);
        // 在 mainLoop 中启动 Acceptor 的监听
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}

// 新连接到来时的处理：由 Acceptor 的回调触发
// sockfd：accept4 返回的新连接 fd
// peerAddr：客户端地址
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // 通过 round-robin 从线程池中选择一个 IO 线程
    EventLoop* ioLoop = threadPool_->getNextLoop();

    // 生成连接名称，如 "EchoServer-127.0.0.1:8080#1"
    char buf[64];
    snprintf(buf, sizeof(buf), "-%s#%d", ipPort.c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;

    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s\n", name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    // 获取本端地址（通过 getsockname 从已连接的 sockfd 获取）
    sockaddr_in local;
    ::memset(&local, 0, sizeof(local));
    socklen_t addrlen = sizeof(local);
    if(::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }

    InetAddress localAddr(local);

    // 创建 TcpConnection 对象（shared_ptr 管理）
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));

    // 存入连接映射表
    connections_[connName] = conn;

    // 设置用户回调
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置关闭回调（连接断开时由 TcpConnection 调用，通知 TcpServer 移除连接）
    conn->setCloseCallback(std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

    // 在连接所属的 IO 线程中执行 connectEstablished（注册 Channel、调用连接回调）
    loop_->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));

}

// 移除连接（可能从任意线程调用，转发到 mainLoop 执行）
void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

// 在 mainLoop 中实际执行连接的移除
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n", name_.c_str(), conn->name().c_str());

    // 从映射表中移除
    connections_.erase(conn->name());

    // 在连接所属的 IO 线程中执行最后的清理（移除 Channel 等）
    // 使用 queueInLoop 而非 runInLoop，确保回调在当前调用栈返回后执行，
    // 此时 TcpConnection 的 shared_ptr 仍被 bind 持有，不会提前析构
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}
