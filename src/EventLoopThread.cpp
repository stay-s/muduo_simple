#include "EventLoopThread.h"

#include "EventLoop.h"

EventLoopThread::EventLoopThread(const ThreadInitCallback &cb, const std::string &name)
    : loop_(nullptr)
    , exiting_(false)
    , thread_(std::bind(&EventLoopThread::threadFunc, this), name)  // 绑定线程函数
    , mutex_()
    , cond_()
    , callback_(cb)
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if(loop_ != nullptr)
    {
        loop_->quit();     // 通知 EventLoop 退出
        thread_.join();    // 等待线程结束
    }
}

// 启动线程并返回新线程中 EventLoop 的指针
// 使用 condition_variable 同步：保证返回时 EventLoop 已经在新线程中创建好了
EventLoop *EventLoopThread::startLoop()
{
    thread_.start();  // 启动新线程，新线程执行 threadFunc()

    // 等待新线程中的 EventLoop 创建完成
    EventLoop *loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // wait 会释放锁并阻塞，直到 lambda 返回 true（即 loop_ 不为空）
        cond_.wait(lock, [this] { return loop_ != nullptr;});
        loop = loop_;
    }
    return loop;
}

// 新线程的入口函数
void EventLoopThread::threadFunc()
{
    // 在新线程的栈上创建 EventLoop
    // 为什么在栈上？因为 EventLoop 构造时会记录当前线程的 TID，
    // 必须在新线程中创建才能正确绑定
    EventLoop loop;

    // 如果有初始化回调（如设置线程优先级等），先执行
    if(callback_)
    {
        callback_(&loop);
    }

    // 通知主线程：EventLoop 已创建好，可以使用了
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }

    // 进入事件循环（阻塞，直到 quit() 被调用）
    loop.loop();

    // loop() 返回说明要退出了，清空指针
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}
