#pragma once

#include "noncopyable.h"
#include "Socket.h"
#include "Channel.h"

class EventLoop;
class InetAddress;

/**
 * Acceptor - 连接接受器
 *
 * 运行在 mainLoop 中，负责监听新连接的到来。
 *
 * 工作原理：
 * 1. 创建一个非阻塞的监听 socket（listenfd）
 * 2. 将 listenfd 封装成 Channel，注册读事件到 mainLoop 的 Poller 上
 * 3. 当有新客户端 connect 时，epoll 检测到 listenfd 可读
 * 4. 调用 handleRead() → accept4() 获得新连接的 fd
 * 5. 通过 NewConnectionCallback 通知 TcpServer 处理新连接
 *
 * Acceptor 不管理连接本身，它只负责"接受"并转交给 TcpServer。
 */
class Acceptor : noncopyable
{
public:
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress&)>;

    Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
    ~Acceptor();

    // 设置新连接到来时的回调（由 TcpServer 设置）
    void setNewConnectionCallback(const NewConnectionCallback& cb) { newConnectionCallback_ = cb;};
    bool listenning() const { return listenning_;}

    // 开始监听（调用 socket 的 listen 并注册读事件）
    void listen();

private:
    // listenfd 可读时的回调：accept 新连接
    void handleRead();

    EventLoop* loop_;              // 所属的 EventLoop（mainLoop）
    Socket acceptSocket_;          // 监听 socket（RAII 管理 listenfd）
    Channel acceptChannel_;        // listenfd 对应的 Channel

    NewConnectionCallback newConnectionCallback_;  // 新连接回调
    bool listenning_;              // 是否正在监听
};
