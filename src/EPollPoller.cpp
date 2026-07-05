#include "EPollPoller.h"
#include "Channel.h"
#include "Logger.h"

#include <errno.h>
#include <unistd.h>
#include <string.h>

// Channel 在 Poller 中的三种状态（用 channel->index() 记录）
const int kNew = -1;    // 从未注册到 Poller（新 Channel 的初始状态）
const int kAdded = 1;   // 已注册在 epoll 中，正在监听事件
const int kDeleted = 2; // 曾经注册过，但已从 epoll 中删除（仍在 channels_ map 中）

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))  // 创建 epoll 实例，CLOEXEC 避免 fd 泄漏给子进程
    , events_(kInitEventListSize)                // 预分配 16 个 epoll_event 的接收空间
{
    if(epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d\n", errno);
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}

// 核心方法：调用 epoll_wait 等待事件，返回就绪的 Channel 列表
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    LOG_INFO("func=%s => fd total count:%lu\n", __FUNCTION__, channels_.size());

    // epoll_wait：阻塞等待事件发生
    // 参数：epollfd, 接收数组, 数组大小, 超时毫秒数
    // 返回值：就绪的事件数量
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno;  // 保存 errno，防止后续操作覆盖
    Timestamp now(Timestamp::now());

    if(numEvents > 0)
    {
        LOG_INFO("%d events happened\n", numEvents);
        fillActiveChannels(numEvents, activeChannels);

        // 如果所有预分配的空间都用满了，说明可能还有更多事件，扩容以备下次
        if(numEvents == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if(numEvents == 0)
    {
        // 超时，没有事件发生
        LOG_DEBUG("%s timeout!\n", __FUNCTION__);
    }
    else
    {
        // 返回 -1 表示出错，但 EINTR（被信号中断）不算错误
        if(saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() error");
        }
    }
    return now;
}

// 注册或更新 Channel 在 epoll 中的事件监听
void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    LOG_INFO("func=%s => fd=%d events=%d index=%d", __FUNCTION__, channel->fd(), channel->events(), index);

    if(index == kNew || index == kDeleted)
    {
        // 情况1：新 Channel 或 之前被删除的 Channel → 添加到 epoll
        if(index == kNew)
        {
            // 新 Channel 需要加入 channels_ map
            int fd = channel->fd();
            channels_[fd] = channel;
        }
        else
        {
            // kDeleted 的 Channel 已经在 channels_ map 中，不需要再加
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);  // 调用 epoll_ctl ADD
    }
    else // index == kAdded：已在 epoll 中
    {
        int fd = channel->fd();
        if(channel->isNoneEvent())
        {
            // 如果不再关注任何事件，从 epoll 中删除（但保留在 map 中以便将来重新添加）
            update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
        }
        else
        {
            // 修改已注册 fd 的关注事件（如从只关注读变为读+写）
            update(EPOLL_CTL_MOD, channel);
        }
    }
}

// 完全移除一个 Channel（从 epoll 和 channels_ map 中都移除）
void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_INFO("func=%s => fd=%d\n", __FUNCTION__, fd);

    int index = channel->index();
    if(index == kAdded)
    {
        // 如果还在 epoll 中监听，先删除
        update(EPOLL_CTL_DEL, channel);
    }
    // 重置为 kNew 状态，如果将来再次使用可以重新注册
    channel->set_index(kNew);
}

// 将 epoll_wait 返回的就绪事件转换为 Channel 列表
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activateChannels) const
{
    for(int i = 0; i < numEvents; ++i)
    {
        // epoll_event.data.ptr 存储的就是注册时传入的 Channel 指针
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        // 设置该 Channel 的实际就绪事件，供 handleEvent 使用
        channel->set_revents(events_[i].events);
        activateChannels->push_back(channel);
    }
}

// 封装 epoll_ctl 系统调用
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    ::memset(&event, 0, sizeof(event));

    int fd = channel->fd();
    event.events = channel->events();   // 要监听的事件
    event.data.ptr = channel;           // 关联 Channel 指针，poll 返回时可以直接取回

    if(::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if(operation == EPOLL_CTL_DEL)
        {
            // 删除失败不致命，只打印错误
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        }
        else
        {
            // 添加/修改失败是致命的，程序无法继续
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
        }
    }
}
