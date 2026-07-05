/**
 * Echo Server 示例 —— 最简单的网络服务器
 *
 * "Echo"（回声）服务器：收到客户端发来的任何数据，原样回送。
 * 这是网络编程的 "Hello World"。
 *
 * 使用方式：
 *   编译后运行 ./testserver，服务器在 8080 端口监听。
 *   用 nc 命令测试：echo "hello" | nc localhost 8080
 *   会收到回复 "hello"。
 *
 * 这个例子展示了 muduo 网络库的基本用法：
 * 1. 创建 EventLoop（事件循环）
 * 2. 创建 TcpServer 绑定到端口
 * 3. 设置回调函数（连接回调、消息回调）
 * 4. 启动服务器
 * 5. 进入事件循环
 */

#include <string>

#include "TcpServer.h"
#include "Logger.h"

class EchoServer
{
public:
    EchoServer(EventLoop* loop, const InetAddress& addr, const std::string& name)
        : server_(loop, addr, name)
        , loop_(loop)
    {
        // 注册回调函数：当有连接建立或断开时调用 onConnection
        server_.setConnectionCallback(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));

        // 注册回调函数：当收到客户端消息时调用 onMessage
        server_.setMessageCallback(std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        // 设置 IO 线程数量为 3
        // 即 mainLoop（1个）负责接受连接 + 3 个 subLoop 负责处理连接的读写
        // 总共 4 个线程
        server_.setThreadNum(3);
    }

    void start()
    {
        server_.start();
    }

private:
    // 连接状态变化回调：新连接建立或连接断开时触发
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());
        }
    }

    // 消息回调：收到客户端数据时触发
    // conn：哪个连接收到的
    // buf：输入缓冲区，包含收到的数据
    // time：消息到达时间
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time)
    {
        // 从缓冲区取出所有数据
        std::string msg(buf->retrieveAllAsString());
        // 原样发送回去（echo）
        conn->send(msg);
    }

private:
    TcpServer server_;
    EventLoop* loop_;

};

int main()
{
    // 创建主事件循环（mainLoop）
    // 这个 EventLoop 运行在主线程中，负责接受新连接
    EventLoop loop;

    // 监听地址：端口 8080，IP 默认 127.0.0.1
    InetAddress addr(8080);

    // 创建 Echo 服务器
    EchoServer server(&loop, addr, "EchoServer");

    // 启动服务器：创建 IO 线程 + 开始监听
    server.start();

    // 进入事件循环（阻塞在这里，直到程序退出）
    // 内部循环：epoll_wait → 处理事件 → 执行回调 → 再次 epoll_wait ...
    loop.loop();

    return 0;
}
