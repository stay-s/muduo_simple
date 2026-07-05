#pragma once

#include <functional>
#include <string>
#include <vector>
#include <memory>

#include "noncopyable.h"

class EventLoop;
class EventLoopThread;

/**
 * EventLoopThreadPool - IO 线程池
 *
 * 管理一组 EventLoopThread，每个线程运行一个独立的 EventLoop。
 * TcpServer 通过它来把新连接分配给不同的 IO 线程。
 *
 * 分配策略：Round-Robin（轮询）
 * 每来一个新连接，按 0, 1, 2, ..., N-1, 0, 1, ... 的顺序分配给各线程。
 * 这样每个线程处理的连接数大致相同。
 *
 * 特殊情况：如果 numThreads_ == 0（未设置线程数），
 * 则不创建任何子线程，所有连接都在 mainLoop 中处理（单线程模式）。
 */
class EventLoopThreadPool : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    EventLoopThreadPool(EventLoop* baseLoop, const std::string& nameArg);
    ~EventLoopThreadPool();

    // 设置 IO 线程数量（必须在 start() 之前调用）
    void setThreadNum(int numThreads) { numThreads_ = numThreads;}

    // 启动线程池：创建 numThreads_ 个线程并运行各自的 EventLoop
    void start(const ThreadInitCallback& cb = ThreadInitCallback());

    // 获取下一个 EventLoop（round-robin 轮询）
    // 如果没有子线程，返回 baseLoop_（即 mainLoop）
    EventLoop* getNextLoop();

    // 获取所有 EventLoop 的列表
    std::vector<EventLoop*> getAllLoops();

    bool started() const { return started_;}
    const std::string& name() const {return name_;}

private:
    EventLoop* baseLoop_;   // mainLoop（用户创建的主循环）
    std::string name_;      // 线程池名称（用于生成线程名）
    bool started_;          // 是否已启动
    int numThreads_;        // IO 线程数量
    int next_;              // round-robin 的当前下标
    std::vector<std::unique_ptr<EventLoopThread>> threads_;  // 所有 IO 线程
    std::vector<EventLoop*> loops_;                          // 所有子 EventLoop 指针
};
