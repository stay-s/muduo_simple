#pragma once

#include <functional>
#include <thread>
#include <memory>
#include <unistd.h>
#include <string>
#include <atomic>

#include "noncopyable.h"

/**
 * Thread - 线程封装类
 *
 * 对 std::thread 的封装，增加了以下功能：
 * 1. 启动同步：通过 POSIX 信号量保证 start() 返回时新线程已经开始执行
 * 2. 线程命名：自动给线程编号（Thread1, Thread2...）
 * 3. 线程 TID 获取：启动后可通过 tid() 获取系统级线程 ID
 *
 * 为什么用 shared_ptr<std::thread> 而不是直接持有 std::thread？
 * 因为 std::thread 不可拷贝，而且需要在 start() 时才创建（而非构造时），
 * 用 shared_ptr 可以延迟创建并安全管理所有权。
 */
class Thread : noncopyable
{
public:
    using ThreadFunc = std::function<void()>;  // 线程执行的函数类型

    explicit Thread(ThreadFunc func, const std::string &name = std::string());

    ~Thread();

    void start();  // 启动线程（会阻塞直到新线程开始运行）
    void join();   // 等待线程结束

    bool started() const {return started_;}
    pid_t tid() const {return tid_;};
    const std::string &name() const {return name_;};

    // 获取已创建的线程总数
    static int numCreated() {return numCreated_;};

private:
    void setDefaultName();  // 设置默认线程名 "ThreadN"

    bool started_;                       // 线程是否已启动
    bool joined_;                        // 是否已调用 join
    std::shared_ptr<std::thread> thread_; // 底层线程对象
    pid_t tid_;                          // 线程的系统 TID
    ThreadFunc func_;                    // 线程要执行的函数
    std::string name_;                   // 线程名称
    static std::atomic_int numCreated_;  // 已创建线程总数（原子变量，线程安全）
};
