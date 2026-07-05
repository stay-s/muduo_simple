#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>

#include "noncopyable.h"
#include "Thread.h"

class EventLoop;

/**
 * EventLoopThread - 一个 EventLoop 绑定一个线程
 *
 * 这个类体现了 "one loop per thread" 的设计：
 * 在一个新线程中创建 EventLoop 并运行 loop()。
 *
 * 启动流程（startLoop）：
 * 1. 调用 thread_.start() 启动新线程
 * 2. 新线程执行 threadFunc()：在栈上创建 EventLoop，通知主线程，然后 loop()
 * 3. 主线程通过 condition_variable 等待新线程创建好 EventLoop 后返回其指针
 *
 * 为什么 EventLoop 在新线程的栈上创建？
 * 因为 EventLoop 的构造函数会记录当前线程的 TID 作为"所属线程"，
 * 如果在主线程创建再传给子线程，TID 就不匹配了。
 */
class EventLoopThread : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback(), const std::string& name = std::string());

    ~EventLoopThread();

    // 启动线程并返回该线程中 EventLoop 的指针
    // 此方法会阻塞，直到新线程中的 EventLoop 创建完毕
    EventLoop* startLoop();

private:
    // 线程主函数：创建 EventLoop 并进入事件循环
    void threadFunc();

    EventLoop* loop_;             // 指向线程中创建的 EventLoop（初始为 nullptr）
    bool exiting_;                // 析构时用于标记退出
    Thread thread_;               // 底层线程对象
    std::mutex mutex_;            // 保护 loop_ 指针
    std::condition_variable cond_; // 同步：等待 EventLoop 创建完成
    ThreadInitCallback callback_; // 线程初始化回调（可选）
};
