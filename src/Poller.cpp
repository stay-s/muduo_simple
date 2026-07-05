#include "Poller.h"
#include "EPollPoller.h"

Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop)
{
}

// 工厂方法：创建默认的 Poller 实现
// 这里直接返回 EPollPoller（Linux 唯一高性能选择）
// 放在 .cpp 中而非头文件，是为了避免基类头文件 include 派生类头文件（反向依赖）
Poller* Poller::newDefaultPoller(EventLoop *loop)
{
    return new EPollPoller(loop);
}
