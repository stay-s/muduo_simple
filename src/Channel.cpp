#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

#include <sys/epoll.h>

// 事件常量定义：映射到 epoll 的事件标志位
const int Channel::kNoneEvent = 0;                    // 不关注任何事件
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;   // 关注读事件（普通数据 + 紧急数据）
const int Channel::kWriteEvent = EPOLLOUT;            // 关注写事件

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)       // 初始不关注任何事件
    , revents_(0)      // 初始无任何就绪事件
    , index_(-1)       // kNew 状态，表示还未注册到 Poller
    , tied_(false)
{
}

Channel::~Channel()
{
}

// 事件处理入口：由 EventLoop 在 poll 返回后调用
// 如果绑定了 tie（TcpConnection 的弱引用），先检查对象是否还活着
void Channel::handleEvent(Timestamp receiveTime)
{
    if(tied_)
    {
        // 尝试将 weak_ptr 提升为 shared_ptr
        // 如果提升成功，说明 TcpConnection 还活着，可以安全处理事件
        // 如果提升失败（返回空），说明 TcpConnection 已被销毁，跳过处理
        std::shared_ptr<void> guard = tie_.lock();
        if(guard)
        {
            handleEventWithGuard(receiveTime);
        }
    }
    else
    {
        // 没有 tie 的 Channel（如 wakeupChannel、acceptChannel）直接处理
        handleEventWithGuard(receiveTime);
    }
}

// 绑定所有者的弱引用（由 TcpConnection::connectEstablished 调用）
void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

// 从 EventLoop 的 Poller 中移除此 Channel
void Channel::remove()
{
    loop_->removeChannel(this);
}

// 当 events_ 被修改后，通知 Poller 更新 epoll 中对应 fd 的事件监听
void Channel::update()
{
    loop_->updateChannel(this);
}

// 根据 Poller 返回的实际事件（revents_）分发到对应的回调函数
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("Channel handleEvent revents:%d\n", revents_);

    // EPOLLHUP：对端关闭（挂起），且没有可读数据
    // 注意：必须同时检查没有 EPOLLIN，因为对端关闭时可能还有最后的数据可读
    if((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if(closeCallback_)
        {
            closeCallback_();
        }
    }

    // EPOLLERR：fd 发生错误
    if(revents_ & EPOLLERR)
    {
        if(errorCallback_)
        {
            errorCallback_();
        }
    }

    // EPOLLIN：有数据可读；EPOLLPRI：有紧急数据（带外数据）
    if(revents_ & (EPOLLIN | EPOLLPRI))
    {
        if(readCallback_)
        {
            readCallback_(receiveTime);
        }
    }

    // EPOLLOUT：可以写数据（内核发送缓冲区有空间了）
    if(revents_ & EPOLLOUT)
    {
        if(writeCallback_)
        {
            writeCallback_();
        }
    }
}
