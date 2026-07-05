#pragma once

#include <functional>
#include <memory>

#include "noncopyable.h"
#include "Timestamp.h"

class EventLoop;

/**
 * Channel - 事件分发器（fd 与回调的桥梁）
 *
 * 在 Reactor 模型中，Channel 是最核心的组件之一。
 * 它不拥有 fd，只是把一个 fd 关联到一组回调函数上。
 *
 * 工作原理：
 * 1. 创建 Channel 时绑定一个 fd 和所属的 EventLoop
 * 2. 通过 setXxxCallback() 设置各种事件的回调函数
 * 3. 通过 enableReading()/enableWriting() 告诉 Poller 关注哪些事件
 * 4. 当 Poller 检测到事件后，调用 handleEvent() 分发到对应的回调
 *
 * 使用场景：
 * - Acceptor 的 listenfd 对应一个 Channel（监听读事件 → 有新连接）
 * - TcpConnection 的 connfd 对应一个 Channel（监听读写事件）
 * - EventLoop 的 wakeupFd 对应一个 Channel（用于跨线程唤醒）
 *
 * tie 机制（防止 use-after-free）：
 * TcpConnection 可能在某个回调中被销毁（如对方关闭连接），但此时 Channel
 * 的 handleEvent 还在执行栈上。tie() 用 weak_ptr 弱引用 TcpConnection，
 * 处理事件前先 lock() 提升为 shared_ptr，如果提升失败说明对象已销毁，
 * 则不再调用回调，避免访问已释放的内存。
 */
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    // 事件到来时由 EventLoop 调用，根据 revents_ 分发到具体回调
    void handleEvent(Timestamp receiveTime);

    // 设置各类事件的回调函数
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 绑定所有者的弱引用，防止回调执行期间所有者被销毁
    void tie(const std::shared_ptr<void> &);

    int fd() const {return fd_;}       // 获取此 Channel 管理的 fd
    int events() const {return events_;}  // 获取关注的事件集合
    void set_revents(int revt) {revents_ = revt;}  // 由 Poller 设置实际发生的事件

    // 注册/取消关注的事件类型（内部调用 update → epoll_ctl）
    void enableReading() { events_ |= kReadEvent; update();}
    void disableReading() { events_ &= ~kReadEvent; update();}
    void enableWriting() { events_ |= kWriteEvent; update();}
    void disableWriting() { events_ &= ~kWriteEvent; update();}
    void disableAll() { events_ = kNoneEvent; update();}

    // 查询当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent;}
    bool isWriting() const { return events_ & kWriteEvent;}
    bool isReading() const { return events_ & kReadEvent;}

    // index 用于 Poller 记录此 Channel 的注册状态
    // kNew(-1)=未注册, kAdded(1)=已注册, kDeleted(2)=已从epoll删除
    int index() { return index_;}
    void set_index(int idx) { index_ = idx;}

    // 从 EventLoop 中移除此 Channel
    void remove();

private:
    void update();  // 通知 EventLoop 更新此 Channel 在 Poller 中的状态
    void handleEventWithGuard(Timestamp receiveTime);  // 带有 tie 保护的事件处理

private:
    ReadEventCallback readCallback_;    // 读事件回调（如有数据到达）
    EventCallback writeCallback_;       // 写事件回调（如可以继续发送）
    EventCallback closeCallback_;       // 对端关闭回调
    EventCallback errorCallback_;       // 错误回调

    EventLoop *loop_;     // 所属的 EventLoop
    const int fd_;        // 管理的文件描述符
    int events_;          // 关注的事件（EPOLLIN/EPOLLOUT 等）
    int revents_;         // Poller 返回的实际发生的事件
    int index_;           // 在 Poller 中的注册状态

    std::weak_ptr<void> tie_;  // 弱引用所有者（通常是 TcpConnection）
    bool tied_;                // 是否已经绑定了所有者

    // 事件常量（在 .cpp 中用 epoll 宏定义）
    static const int kNoneEvent;   // 无事件 = 0
    static const int kReadEvent;   // 读事件 = EPOLLIN | EPOLLPRI
    static const int kWriteEvent;  // 写事件 = EPOLLOUT
};
