#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"

class Channel;
class Poller;

/**
 * EventLoop - 事件循环（Reactor 的核心）
 *
 * 每个 EventLoop 对象绑定一个线程（one loop per thread），
 * 在该线程中驱动整个事件循环。
 *
 * 核心工作流程（loop() 方法）：
 *   while (!quit_) {
 *       1. poller_->poll()    -- 调用 epoll_wait 等待事件
 *       2. handleEvent()      -- 遍历就绪的 Channel，调用各自回调
 *       3. doPendingFunctors() -- 执行其他线程投递过来的任务
 *   }
 *
 * 跨线程调用机制：
 * 网络库中经常需要从线程 A 让线程 B 执行某个操作（如在 IO 线程中关闭连接）。
 * 实现方式：
 *   1. 线程 A 调用 queueInLoop(cb)，把回调放入 pendingFunctors_ 队列
 *   2. 通过 wakeup() 向 eventfd 写入数据，唤醒正在 epoll_wait 的线程 B
 *   3. 线程 B 被唤醒后在 doPendingFunctors() 中依次执行队列中的回调
 *
 * eventfd 是 Linux 提供的轻量级线程间通知机制：
 * - 写入一个 uint64_t 值表示"有事情要做"
 * - 读取将值清零，epoll 检测到它可读就会唤醒线程
 * - 比传统的 pipe 更高效（只需一个 fd）
 */
class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;  // 待执行的回调函数类型

    EventLoop();
    ~EventLoop();

    // 进入事件循环（阻塞，直到 quit() 被调用）
    void loop();
    // 退出事件循环
    void quit();

    // 获取最近一次 poll 返回的时间
    Timestamp pollReturnTime() const { return pollReturnTime_; };

    // 在当前 loop 所在线程中执行回调 cb
    // 如果调用者就在当前线程，直接执行；否则放入队列等待执行
    void runInLoop(Functor cb);

    // 将回调放入队列，并唤醒 loop 所在线程
    void queueInLoop(Functor cb);

    // 唤醒 loop 所在线程（向 eventfd 写入数据触发 epoll）
    void wakeup();

    // 供 Channel 调用，通知 Poller 更新/移除 Channel
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);

    // 判断当前代码是否运行在 loop 所属线程中
    // 这是保证线程安全的关键——很多操作只能在 loop 线程中执行
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

private:
    // eventfd 的读回调：读取 8 字节数据以清除可读状态
    void handleRead();

    // 依次执行 pendingFunctors_ 中的所有回调
    void doPendingFunctors();

private:
    Timestamp pollReturnTime_;      // poll 返回的时间戳
    const pid_t threadId_;          // 创建此 EventLoop 的线程 ID（用于判断跨线程）

    using ChannelList = std::vector<Channel *>;
    ChannelList activeChannels_;    // Poller 返回的就绪 Channel 列表

    std::atomic_bool looping_;      // 是否正在循环中
    std::atomic_bool quit_;         // 是否要退出循环

    std::unique_ptr<Poller> poller_;         // I/O 多路复用器
    std::unique_ptr<Channel> wakeupChannel_; // eventfd 对应的 Channel
    int wakeupFd_;                           // 用于跨线程唤醒的 eventfd

    // 跨线程任务队列（需要加锁保护）
    std::atomic_bool callingPendingFunctors_; // 是否正在执行 pending 回调
    std::vector<Functor> pendingFunctors_;    // 待执行的回调队列
    std::mutex mutex_;                        // 保护 pendingFunctors_ 的互斥锁
};
