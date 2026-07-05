#pragma once

#include <memory>
#include <string>
#include <atomic>

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"

class Channel;
class EventLoop;
class Socket;

/**
 * TcpConnection - TCP 连接
 *
 * 代表一个已建立的 TCP 连接，是用户与网络库交互的核心对象。
 * 每个 TcpConnection 由 shared_ptr 管理生命周期（所以继承 enable_shared_from_this）。
 *
 * 拥有的资源：
 * - Socket：RAII 管理 connfd
 * - Channel：注册到所属 EventLoop 的 Poller 上，监听读写事件
 * - inputBuffer_：接收缓冲区（从 socket 读入的数据暂存于此）
 * - outputBuffer_：发送缓冲区（一次写不完的数据暂存于此）
 *
 * 状态机：
 * kConnecting → kConnected → kDisconnecting → kDisconnected
 *                              (shutdown)        (handleClose)
 *
 * 关键流程：
 * - 数据到达：handleRead → readFd → messageCallback（通知用户）
 * - 发送数据：send → sendInLoop → write/outputBuffer → handleWrite
 * - 关闭连接：handleClose → disableAll → closeCallback → removeConnection
 *
 * 高水位回调（HighWaterMark）：
 * 当 outputBuffer_ 中积压的数据超过阈值（默认64MB）时触发，
 * 提醒用户"对方接收慢了，请降低发送速度"，防止内存无限增长。
 *
 * enable_shared_from_this 的作用：
 * 当需要在回调中传递自身的 shared_ptr 时（如注册到 EventLoop），
 * 不能直接 shared_ptr<TcpConnection>(this)（会产生两个独立的引用计数），
 * 而要用 shared_from_this() 获取与外部共享同一引用计数的 shared_ptr。
 */
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop* loop, const std::string& name, int sockfd, const InetAddress& localAddr, const InetAddress& peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_;}
    const std::string& name() const { return name_;}
    const InetAddress& localAddress() const { return localAddr_;}
    const InetAddress& peerAddress() const { return peerAddr_;}

    bool connected() const { return state_ == kConnected;}

    // 发送数据（线程安全，可从任意线程调用）
    void send(const std::string& buf);
    // 发送文件内容（零拷贝，使用 Linux sendfile 系统调用）
    void sendFile(int fileDescriptor, off_t offset, size_t count);

    // 主动关闭连接（半关闭：关闭写端，等对方读完后自然断开）
    void shutdown();

    // 设置各种回调（由 TcpServer 在创建连接时设置）
    void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb;}
    void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb;}
    void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb;}
    void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb;}
    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark) { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark;}

    // 连接建立完成时调用（注册 Channel 到 Poller，调用用户的连接回调）
    void connectEstablished();
    // 连接销毁时调用（从 Poller 中移除 Channel）
    void connectDestroyed();

private:
    // 连接状态枚举
    enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting};

    void setState(StateE s) { state_ = s;}

    // Channel 的四个事件回调
    void handleRead(Timestamp receiveTime);  // 有数据可读
    void handleWrite();                       // 可以继续写数据
    void handleClose();                       // 对端关闭
    void handleError();                       // 发生错误

    // 在 IO 线程中执行的实际发送/关闭逻辑
    void sendInLoop(const void* data, size_t len);
    void shutdownInLoop();
    void sendFileInLoop(int fileDescriptor, off_t offset, size_t count);

private:
    EventLoop* loop_;          // 此连接所属的 EventLoop（某个 subLoop）
    const std::string name_;   // 连接名称（如 "EchoServer-127.0.0.1:8080#1"）
    std::atomic_int state_;    // 连接状态
    bool reading_;

    std::unique_ptr<Socket> socket_;    // 管理 connfd
    std::unique_ptr<Channel> channel_;  // connfd 对应的 Channel

    const InetAddress localAddr_;  // 本端地址
    const InetAddress peerAddr_;   // 对端地址

    // 用户设置的回调
    ConnectionCallback connectionCallback_;       // 连接状态变化回调
    MessageCallback messageCallback_;             // 收到消息回调
    WriteCompleteCallback writeCompleteCallback_; // 数据发送完毕回调
    HighWaterMarkCallback highWaterMarkCallback_; // 发送缓冲区超过阈值回调
    CloseCallback closeCallback_;                 // 连接关闭回调（TcpServer 设置）
    size_t highWaterMark_;                        // 高水位阈值（默认 64MB）

    Buffer inputBuffer_;   // 接收缓冲区：从 socket 读入的数据暂存于此
    Buffer outputBuffer_;  // 发送缓冲区：一次写不完的数据暂存于此
};
