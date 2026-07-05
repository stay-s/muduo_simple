#include "EventLoopThreadPool.h"

#include "EventLoop.h"
#include "EventLoopThread.h"
#include "Logger.h"

#include <memory>

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg)
    : baseLoop_(baseLoop)
    , name_(nameArg)
    , started_(false)
    , numThreads_(0)
    , next_(0)  // round-robin 计数器
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // EventLoop 对象在各自线程的栈上创建，线程结束时自动销毁，这里无需手动释放
}

// 启动线程池：创建 numThreads_ 个 IO 线程
void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;
    for(int i = 0; i < numThreads_; ++i)
    {
        // 给每个线程命名（如 "EchoServer0", "EchoServer1"...）
        char buf[name_.size() + 32];
        snprintf(buf, sizeof(buf), "%s%d", name_.c_str(), i);

        // 创建 EventLoopThread 并启动
        EventLoopThread *t = new EventLoopThread(cb, buf);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        // startLoop() 会阻塞直到新线程中的 EventLoop 创建完成
        loops_.push_back(t->startLoop());
    }

    // 如果没有创建子线程（numThreads_ == 0），执行用户的初始化回调在 mainLoop 上
    if(numThreads_ ==0 && cb)
    {
        cb(baseLoop_);
    }
}

// 获取下一个 EventLoop（round-robin 轮询分配）
EventLoop *EventLoopThreadPool::getNextLoop()
{
    // 默认返回 mainLoop（单线程模式，或作为 fallback）
    EventLoop *loop = baseLoop_;

    if(!loops_.empty())
    {
        // 按顺序轮询选择下一个 subLoop
        loop = loops_[next_];
        ++next_;
        // 到末尾后回到开头（循环）
        if(static_cast<size_t>(next_) >= loops_.size())
        {
            next_ = 0;
        }
    }

    return loop;
}

// 获取所有 EventLoop 指针（包括单线程模式下只有 baseLoop 的情况）
std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    if(loops_.empty())
    {
        return std::vector<EventLoop*>(1, baseLoop_);
    }
    else
    {
        return loops_;
    }
}
