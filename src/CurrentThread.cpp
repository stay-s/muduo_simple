#include "CurrentThread.h"

namespace CurrentThread
{
    // 线程局部变量：每个线程有自己独立的一份，互不影响
    __thread int t_cachedTid = 0;

    // 通过系统调用获取当前线程的 TID 并缓存
    // syscall(SYS_gettid) 是唯一获取内核级线程 ID 的方式
    // （pthread_self 返回的是用户态线程库的标识，不一定等于系统 TID）
    void cacheTid(){
        if(t_cachedTid == 0)
        {
            t_cachedTid = static_cast<int>(::syscall(SYS_gettid));
        }
    }
}
