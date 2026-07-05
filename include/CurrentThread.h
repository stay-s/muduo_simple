#pragma once

#include <unistd.h>
#include <sys/syscall.h>

/**
 * CurrentThread - 当前线程信息
 *
 * 提供获取当前线程 TID（线程ID）的功能。
 *
 * 为什么不用 std::this_thread::get_id()？
 * 因为 Linux 的 gettid() 返回的是操作系统级别的线程 ID（一个整数），
 * 可以用 top、ps 等工具直接观察，方便调试。
 *
 * __thread 关键字：线程局部存储（TLS），每个线程有自己独立的一份副本。
 * 第一次调用 tid() 时通过系统调用获取 TID 并缓存，后续直接返回缓存值。
 *
 * __builtin_expect：GCC 内建函数，用于分支预测优化。
 * __builtin_expect(x, 0) 表示 x 大概率为 false，帮助 CPU 预取正确的分支。
 */
namespace CurrentThread
{
    // 线程局部变量：缓存当前线程的 TID，避免每次都做系统调用
    extern __thread int t_cachedTid;

    // 通过 syscall(SYS_gettid) 获取线程 ID 并缓存
    void cacheTid();

    // 获取当前线程 TID（首次调用会触发 cacheTid）
    // 我预测 t_cachedTid == 0 这件事大概率是假的（即不成立，TID 已经缓存好了）
    inline int tid(){
        if(__builtin_expect(t_cachedTid == 0, 0))
        {
            cacheTid();
        }
        return t_cachedTid;
    }
}
