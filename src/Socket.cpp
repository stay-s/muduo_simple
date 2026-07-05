#include "Socket.h"
#include "InetAddress.h"
#include "Logger.h"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <string.h>

Socket::~Socket()
{
    // RAII：析构时自动关闭 fd，防止资源泄漏
    ::close(sockfd_);
}

// 将 socket 绑定到一个本地地址（IP + 端口）
void Socket::bindAddress(const InetAddress &localaddr)
{
    if(0 != ::bind(sockfd_, (sockaddr*)localaddr.getSockAddr(), sizeof(sockaddr_in)))
    {
        LOG_FATAL("bind sockfd:%d fail", sockfd_);
    }
}

// 开始监听连接请求
// backlog=1024：内核维护的未完成连接队列最大长度
void Socket::listen()
{
    if(0 != ::listen(sockfd_, 1024))
    {
        LOG_FATAL("listen sockfd:%d fail", sockfd_);
    }
}

// 接受一个新连接，返回连接的 fd
// 使用 accept4 而非 accept：可以直接设置新 fd 为非阻塞（避免额外的 fcntl 调用）
int Socket::accept(InetAddress *peeraddr)
{
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    ::memset(&addr, 0, sizeof(addr));

    // accept4：Linux 特有，接受连接的同时设置 fd 标志
    // SOCK_NONBLOCK：非阻塞（Reactor 模型要求）
    // SOCK_CLOEXEC：exec 时关闭（安全考虑）
    int connfd = ::accept4(sockfd_, (sockaddr*)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if(connfd >= 0)
    {
        peeraddr->setSockAddr(addr);  // 输出参数：客户端地址
    }
    return connfd;
}

// 关闭写端（半关闭）：通知对方"我不再发送数据了"
// 对方的 read 会返回 0，但我方仍可以接收数据
void Socket::shutdownWrite()
{
    if(::shutdown(sockfd_, SHUT_WR) < 0)
    {
        LOG_ERROR("shutdownWrite error");
    }
}

// TCP_NODELAY：禁用 Nagle 算法
// Nagle 算法会将小数据包合并后再发送（减少网络包数量），但增加了延迟。
// 对于实时性要求高的场景（如游戏、交互式应用），应该禁用它。
void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

// SO_REUSEADDR：允许重用处于 TIME_WAIT 状态的地址
// 服务器重启时，旧的连接可能还在 TIME_WAIT 状态（持续约 2 分钟），
// 不设置此选项会导致 bind 失败："Address already in use"
void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

// SO_REUSEPORT：允许多个 socket 绑定到同一个端口
// 内核会自动将新连接负载均衡到绑定同一端口的不同 socket 上。
// 可用于多进程/多线程服务器的负载均衡。
void Socket::setReusePort(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}

// SO_KEEPALIVE：启用 TCP 心跳检测
// 启用后，如果连接空闲了一段时间（默认 2 小时），TCP 会自动发送探测包。
// 如果对方没响应，内核认定连接已死并通知应用层。
// 用于检测网络故障或对端崩溃导致的"半开连接"。
void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}
