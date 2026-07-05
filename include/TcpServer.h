#pragma once

#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "EventLoopThreadPool.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

/**
 * TcpServer - TCP 服务器（用户直接使用的顶层类）
 *
 * 用户使用步骤：
 * 1. 创建 EventLoop（主循环）
 * 2. 创建 TcpServer，绑定地址和端口
 * 3. 设置回调函数（onConnection, onMessage 等）
 * 4. 设置线程数 setThreadNum()
 * 5. 调用 start() 开始监听
 * 6. 调用 loop.loop() 进入事件循环
 *
 * 内部组成：
 * - Acceptor：监听新连接（运行在 mainLoop）
 * - EventLoopThreadPool：管理 IO 线程池
 * - ConnectionMap：存储所有活跃的 TcpConnection
 *
 * 新连接处理流程：
 * Acceptor 接受连接 → newConnection() 被调用 →
 * 选择一个 subLoop → 创建 TcpConnection → 注册回调 →
 * 在 subLoop 中执行 connectEstablished()
 *
 * 连接关闭流程：
 * TcpConnection::handleClose() → closeCallback（即 removeConnection）→
 * 从 ConnectionMap 中移除 → 在 subLoop 中执行 connectDestroyed()
 */
class TcpServer
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    enum Option
    {
        kNoReusePort,  // 不启用端口复用
        kReusePort     // 启用端口复用（多线程/多进程绑定同一端口）
    };

    TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& nameArg, Option option = kNoReusePort);
    ~TcpServer();

    // 设置各种回调
    void setThreadInitCallback(const ThreadInitCallback& cb) { threadInitCallback_ = cb;}
    void setConnectionCallback(const ConnectionCallback& cb) { connectionCallback_ = cb;}
    void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb;}
    void setWriteCompleteCallback(const WriteCompleteCallback& cb) { writeCompleteCallback_ = cb;}

    // 设置 IO 线程数量（0 = 单线程模式，所有工作在 mainLoop 完成）
    void setThreadNum(int numThreads);

    // 启动服务器（可被多次调用，但只有第一次生效）
    void start();

private:
    // Acceptor 的新连接回调：创建 TcpConnection 并分配到 subLoop
    void newConnection(int sockfd, const InetAddress& peerAddr);

    // 移除一个连接（线程安全包装）
    void removeConnection(const TcpConnectionPtr& conn);
    // 在 mainLoop 中执行实际的移除操作
    void removeConnectionInLoop(const TcpConnectionPtr& conn);

private:
    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop* loop_;      // mainLoop（用户传入的主事件循环）
    const std::string name_;   // 服务器名称
    const std::string ipPort;  // 监听地址字符串（如 "127.0.0.1:8080"）

    std::unique_ptr<Acceptor> acceptor_;            // 连接接受器
    std::shared_ptr<EventLoopThreadPool> threadPool_; // IO 线程池

    // 用户设置的回调
    ConnectionCallback connectionCallback_;         // 连接状态回调
    MessageCallback messageCallback_;               // 消息到达回调
    WriteCompleteCallback writeCompleteCallback_;   // 写完成回调

    ThreadInitCallback threadInitCallback_;  // 线程初始化回调
    int numThreads_;                         // IO 线程数量
    std::atomic_int started_;                // 是否已启动（保证只启动一次）
    int nextConnId_;                         // 下一个连接的编号
    ConnectionMap connections_;              // 所有活跃连接的映射表（name → TcpConnectionPtr）
};
