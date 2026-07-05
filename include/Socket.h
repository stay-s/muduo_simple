#pragma once

#include "noncopyable.h"

class InetAddress;

/**
 * Socket - RAII 封装的套接字类
 *
 * RAII（Resource Acquisition Is Initialization）：
 * 在构造时获取资源（这里是 fd），在析构时释放资源（close(fd)）。
 * 这样即使发生异常，也能保证 fd 被正确关闭，不会泄漏。
 *
 * 这个类封装了服务器端 socket 的常用操作：
 * - bind：把 socket 绑定到一个地址（IP + 端口）
 * - listen：开始监听连接请求
 * - accept：接受一个新的客户端连接
 * - shutdownWrite：关闭写端（通知对方"我不再发送数据了"）
 *
 * 以及四个重要的 socket 选项：
 * - TCP_NODELAY：禁用 Nagle 算法（减少延迟）
 * - SO_REUSEADDR：允许重用处于 TIME_WAIT 状态的地址
 * - SO_REUSEPORT：允许多个 socket 绑定同一端口（负载均衡）
 * - SO_KEEPALIVE：启用 TCP 心跳检测死连接
 */
class Socket : noncopyable
{
public:
    explicit Socket(int sockfd)
        : sockfd_(sockfd)
    {
    }
    ~Socket();  // 析构时 close(sockfd_)

    int fd() const { return sockfd_; }

    void bindAddress(const InetAddress &localaddr);  // 绑定地址
    void listen();                                    // 开始监听
    int accept(InetAddress *peeraddr);                // 接受新连接，返回 connfd

    void shutdownWrite();  // 关闭写端（半关闭）

    void setTcpNoDelay(bool on);   // 禁用 Nagle 算法，减少小包延迟
    void setReuseAddr(bool on);    // 允许重用 TIME_WAIT 地址
    void setReusePort(bool on);    // 允许多个进程/线程绑定同一端口
    void setKeepAlive(bool on);    // 启用 TCP 心跳

private:
    const int sockfd_;  // 持有的文件描述符
};
