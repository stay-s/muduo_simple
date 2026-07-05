#include "EventLoop.h"
#include "Logger.h"
#include "Channel.h"
#include "Poller.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

// 线程局部变量：保证每个线程最多只有一个 EventLoop 实例
// __thread 表示每个线程有自己独立的一份，互不干扰
__thread EventLoop *t_loopInThisThread = nullptr;

// epoll_wait 的超时时间（毫秒），超时后返回以便执行 doPendingFunctors
const int kPollTimeMs = 10000;

// 创建 eventfd，用于线程间的唤醒通知
// eventfd 是一个计数器 fd，写入时计数增加（触发可读），读取时计数清零
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evtfd < 0)
    {
        LOG_FATAL("Failed in eventfd");
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())         // 记录创建此 EventLoop 的线程 ID
    , poller_(Poller::newDefaultPoller(this))  // 创建 epoll 实例
    , wakeupFd_(createEventfd())              // 创建唤醒用的 eventfd
    ,wakeupChannel_(new Channel(this, wakeupFd_))  // 为 eventfd 创建 Channel
{
    LOG_DEBUG("EventLoop created %p in thread %d\n", this, threadId_);

    // 检查当前线程是否已经有 EventLoop 了（一个线程只能有一个）
    if(t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists int this thread %d\n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    // 设置 wakeupChannel 的读回调，并开始监听它的读事件
    // 当其他线程调用 wakeup() 写入数据时，epoll_wait 会因为 wakeupFd 可读而返回
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    // 清理 wakeupChannel：取消所有事件监听并从 Poller 中移除
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 事件循环的主体——Reactor 的核心
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping\n", this);

    while(!quit_)
    {
        // 第一步：清空上一轮的就绪列表
        activeChannels_.clear();

        // 第二步：调用 epoll_wait 等待事件（阻塞直到有事件或超时）
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);

        // 第三步：遍历所有就绪的 Channel，调用各自的回调函数处理事件
        for(Channel *channel : activeChannels_)
        {
            channel->handleEvent(pollReturnTime_);
        }

        // 第四步：执行其他线程投递过来的回调任务
        // 为什么放在这里？因为这些任务可能会修改 Channel 的状态，
        // 必须在 handleEvent 之后执行，避免干扰当前轮次的事件处理
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping\n", this);
    looping_ = false;
}

// 退出事件循环
void EventLoop::quit()
{
    quit_ = true;
    // 如果是其他线程调用的 quit()，需要唤醒正在 epoll_wait 阻塞的 loop 线程
    if(!isInLoopThread())
    {
        wakeup();
    }
}

// 在 loop 线程中执行回调
// 如果当前就在 loop 线程中，直接执行；否则放入队列等待
void EventLoop::runInLoop(Functor cb)
{
    if(isInLoopThread())
    {
        cb();
    }
    else
    {
        queueInLoop(std::move(cb));
    }
}

// 将回调加入队列，并在必要时唤醒 loop 线程
void EventLoop::queueInLoop(Functor cb)
{
    // 加锁保护队列（多个线程可能同时向队列添加回调）
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(std::move(cb));
    }

    // 两种情况需要唤醒：
    // 1. 调用者不在 loop 线程中（loop 线程可能正在 epoll_wait 阻塞）
    // 2. loop 线程正在执行 doPendingFunctors（本轮 functors 执行完后会再次 poll，
    //    如果不唤醒，新加入的 functor 要等到下次超时才能执行）
    if(!isInLoopThread() || callingPendingFunctors_)
    {
        wakeup();
    }
}

// 读取 eventfd 的数据（清除可读状态），否则 epoll 会持续触发
void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = ::read(wakeupFd_, &one, sizeof(one));
    if(n != sizeof(one))
    {
        LOG_ERROR("EventLoop::handleRead() read %lu bytes instead of 8\n", n);
    }
}

// 唤醒 loop 线程：向 eventfd 写入 8 字节数据
// 这会让 eventfd 变为可读状态，从而让 epoll_wait 立即返回
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
    if(n != sizeof(one))
    {
        LOG_ERROR("EventLoop::wakeup() write %lu bytes instead of 8\n", n);
    }
}

// 转发 Channel 的更新请求给 Poller
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

// 执行 pendingFunctors_ 队列中的所有回调
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    // 用 swap 把队列内容取出来，然后立即释放锁
    // 这样其他线程可以继续往队列中添加回调，不会被阻塞
    // 同时避免了在持有锁的情况下执行回调（回调中可能再次调用 queueInLoop）
    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for(const Functor &functor : functors)
    {
        functor();
    }

    callingPendingFunctors_ = false;
}
