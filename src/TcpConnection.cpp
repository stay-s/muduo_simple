#include "TcpConnection.h"
#include "EventLoop.h"
#include "Logger.h"
#include "Channel.h"
#include "Socket.h"

#include <functional>
#include <string>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <fcntl.h>  // for open
#include <unistd.h> // for close

static EventLoop *checkLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null!\n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

// 构造函数：初始化连接的所有组件
TcpConnection::TcpConnection(EventLoop *loop, const std::string &name, int sockfd, const InetAddress &localAddr, const InetAddress &peerAddr)
    : loop_(checkLoopNotNull(loop))
    , name_(name)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
    , highWaterMark_(64 * 1024 * 1024) // 高水位阈值默认 64MB
{
    // 给 Channel 设置回调：当 fd 上有事件时，Channel 调用 TcpConnection 的处理方法
    channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);  // 启用 TCP 心跳，检测死连接
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d\n", name_.c_str(), channel_->fd(), (int)state_);
}

// 发送数据（对外接口，线程安全）
// 如果在当前 IO 线程中，直接发送；否则转发到 IO 线程执行
void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
        }
    }
}

// 发送文件（零拷贝：使用 sendfile 系统调用，数据不经过用户空间）
void TcpConnection::sendFile(int fileDescriptor, off_t offset, size_t count)
{
    if(connected())
    {
        if(loop_->isInLoopThread())
        {
            sendFileInLoop(fileDescriptor, offset, count);
        }
        else
        {
            loop_->runInLoop(std::bind(&TcpConnection::sendFileInLoop, this, fileDescriptor, offset, count));
        }
    }
    else
    {
        LOG_ERROR("TcpConnection::sendFile - not connected");
    }
}

// 主动关闭连接（优雅关闭：先关写端，等对方读完再断开）
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

// 连接建立完成时调用（由 TcpServer 在 IO 线程中调用）
void TcpConnection::connectEstablished()
{
        setState(kConnected);

        // tie：Channel 弱引用 TcpConnection，防止 handleEvent 期间对象被销毁
        channel_->tie(shared_from_this());

        // 注册读事件到 epoll，开始接收数据
        channel_->enableReading();

        // 调用用户的连接回调（通知"连接已建立"）
        connectionCallback_(shared_from_this());
}

// 连接销毁时调用（由 TcpServer::removeConnectionInLoop 在 IO 线程中调用）
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kConnected);
        channel_->disableAll();  // 取消所有事件监听
        connectionCallback_(shared_from_this());  // 通知用户连接断开
    }

    // 从 Poller 中移除 Channel
    channel_->remove();
}

// === 以下是 Channel 的四个事件回调 ===

// 读事件：socket 有数据可读
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    // 从 fd 读取数据到输入缓冲区
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if(n > 0)
    {
        // 读到数据，调用用户的消息回调
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if(n == 0)
    {
        // read 返回 0 表示对端关闭了连接
        handleClose();
    }
    else
    {
        // read 出错
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead\n");
        handleError();
    }
}

// 写事件：socket 可以继续写数据了（之前因为内核缓冲区满而暂停）
void TcpConnection::handleWrite()
{
    if(channel_->isWriting())
    {
        int savedErrno = 0;
        // 把输出缓冲区中的数据写到 socket
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if(n > 0)
        {
            // 标记已写出 n 字节
            outputBuffer_.retrieve(n);

            if(outputBuffer_.readableBytes() == 0)
            {
                // 输出缓冲区清空了，取消写事件监听（否则 epoll 会一直通知可写）
                channel_->disableWriting();

                // 通知用户：数据全部发送完毕
                if(writeCompleteCallback_)
                {
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }

                // 如果正在关闭连接（用户调用了 shutdown），现在缓冲区空了可以执行关闭
                if(state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite\n");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing\n", channel_->fd());
    }
}

// 对端关闭连接
void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose state=%d\n", (int)state_);
    setState(kDisconnected);
    channel_->disableAll();  // 取消所有事件监听

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr);  // 通知用户连接断开
    closeCallback_(connPtr);       // 通知 TcpServer 移除此连接
}

// 连接发生错误
void TcpConnection::handleError()
{
    int optionVal;
    socklen_t optionLen = sizeof(optionVal);
    int err = 0;
    // 通过 getsockopt 获取 socket 上的具体错误码
    if(::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optionVal, &optionLen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optionVal;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d\n", name_.c_str(), err);
}

// 在 IO 线程中实际执行的发送逻辑
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    if (state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing!\n");
    }

    // 优化：如果输出缓冲区为空且没有正在监听写事件，先尝试直接写
    // 大多数情况下，一次 write 就能发送完所有数据（内核缓冲区够大）
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 一次就写完了，通知用户
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop\n");
                // EPIPE：对端已关闭；ECONNRESET：连接被重置
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    faultError = true;
                }
            }
        }
    }

    // 如果没有出错，且还有剩余数据没发完
    if (!faultError && remaining > 0)
    {
        // 检查是否触发高水位回调
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_)
        {
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }

        // 把剩余数据追加到输出缓冲区
        outputBuffer_.append((char *)data + nwrote, remaining);
        if (!channel_->isWriting())
        {
            // 注册写事件：等内核缓冲区有空间时，epoll 通知 handleWrite 继续发送
            channel_->enableWriting();
        }
    }
}

// 关闭写端（半关闭）
void TcpConnection::shutdownInLoop()
{
    // 只有输出缓冲区清空后才能关闭写端，否则数据会丢失
    if (!channel_->isWriting())
    {
        socket_->shutdownWrite();
    }
}

// 使用 sendfile 发送文件（零拷贝：数据直接从文件传到 socket，不经过用户空间）
void TcpConnection::sendFileInLoop(int fileDescriptor, off_t offset, size_t count)
{
    ssize_t bytesSent = 0;
    size_t remaining = count;
    bool faultError = false;

    if(state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing!\n");
        return;
    }

    // 只有输出缓冲区为空时才能使用 sendfile（不能和缓冲区数据混在一起）
    if(!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        bytesSent = sendfile(channel_->fd(), fileDescriptor, &offset, count);
        if(bytesSent >= 0)
        {
            remaining = count - bytesSent;
            if(remaining == 0 && writeCompleteCallback_)
            {
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else
        {
            if(errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendFileInLoop\n");
            }
            if(errno == EPIPE || errno == ECONNRESET)
            {
                faultError = true;
            }
        }
    }

    // 如果没发完，稍后重试（通过 queueInLoop 再次调度）
    if(!faultError && remaining > 0)
    {
        loop_->queueInLoop(std::bind(&TcpConnection::sendFileInLoop, this, fileDescriptor, offset, remaining));
    }
}
