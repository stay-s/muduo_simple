#pragma once

#include <vector>
#include <sys/epoll.h>

#include "Poller.h"

/**
 * EPollPoller - 基于 epoll 的 I/O 多路复用实现
 *
 * epoll 是 Linux 特有的高性能 I/O 多路复用机制。
 * 与 select/poll 不同，epoll 使用事件驱动方式，不需要每次都遍历所有 fd。
 *
 * epoll 的三个核心系统调用：
 * - epoll_create1：创建一个 epoll 实例，返回一个 epollfd
 * - epoll_ctl：向 epoll 注册/修改/删除要监听的 fd 及其事件
 * - epoll_wait：阻塞等待事件发生，返回就绪的 fd 列表
 *
 * 本实现的 LT（Level Trigger，水平触发）模式：
 * 只要 fd 的缓冲区里有数据可读，epoll_wait 就会持续通知，
 * 直到数据被读完。这比 ET（Edge Trigger）模式更容易编程，不会漏掉数据。
 *
 * events_ 是一个动态数组，用来接收 epoll_wait 返回的就绪事件。
 * 初始大小为 16，当一次返回的事件数等于数组大小时，自动翻倍扩容。
 */
class EPollPoller : public Poller
{
public:
    EPollPoller(EventLoop *loop);
    ~EPollPoller() override;

    // 调用 epoll_wait 等待事件，将就绪的 Channel 填入 activeChannels
    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;

    // 注册/更新一个 Channel 到 epoll（内部调用 epoll_ctl ADD/MOD）
    void updateChannel(Channel *channel) override;

    // 从 epoll 中移除一个 Channel（内部调用 epoll_ctl DEL）
    void removeChannel(Channel *channel) override;

private:
    static const int kInitEventListSize = 16;  // events_ 初始容量

    // 遍历 epoll_wait 返回的就绪事件，找到对应 Channel 放入列表
    void fillActiveChannels(int numEvents, ChannelList *activateChannels) const;

    // 调用 epoll_ctl 执行具体的 ADD/MOD/DEL 操作
    void update(int operation, Channel *channel);

    using EventList = std::vector<epoll_event>;

    int epollfd_;      // epoll 实例的文件描述符
    EventList events_; // 接收 epoll_wait 返回结果的数组
};
