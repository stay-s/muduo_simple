#pragma once

#include <vector>
#include <unordered_map>

#include "noncopyable.h"
#include "Timestamp.h"

class Channel;
class EventLoop;

/**
 * Poller - I/O 多路复用的抽象基类
 *
 * 为什么要有抽象基类？
 * Linux 上常用的 I/O 多路复用有 poll 和 epoll 两种，抽象出基类后
 * 可以通过工厂方法 newDefaultPoller() 来选择具体实现，不用改上层代码。
 * 本项目只实现了 EPollPoller。
 *
 * Poller 是 EventLoop 的成员，负责：
 * 1. 监听哪些 fd 上有事件发生
 * 2. 返回就绪的 Channel 列表给 EventLoop 去分发
 *
 * channels_ 是一个 fd → Channel* 的映射表，
 * Poller 通过它来管理所有注册过的 Channel。
 */
class Poller
{
public:
    using ChannelList = std::vector<Channel *>;

    Poller(EventLoop *loop);
    virtual ~Poller() = default;

    // 等待 I/O 事件，返回就绪的 Channel 列表（纯虚函数，子类实现）
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;

    // 添加/更新一个 Channel 的事件监听
    virtual void updateChannel(Channel *channel) = 0;

    // 从监听列表中移除一个 Channel
    virtual void removeChannel(Channel *channel) = 0;

    // 工厂方法：创建默认的 Poller 实现（返回 EPollPoller）
    static Poller *newDefaultPoller(EventLoop *loop);

protected:
    // fd → Channel* 映射，存储所有注册过的 Channel
    using ChannelMap = std::unordered_map<int, Channel *>;
    ChannelMap channels_;

private:
    EventLoop *ownerLoop_;  // Poller 所属的 EventLoop
};
