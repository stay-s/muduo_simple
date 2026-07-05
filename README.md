# Muduo 网络库（简化版）

一个基于 **Reactor 模式** 的 Linux C++ 多线程网络库，简化自陈硕的 muduo 库。

## 什么是网络编程？

网络编程的核心问题是：**如何让一个程序同时处理成千上万个网络连接？**

最朴素的做法是"一个连接一个线程"，但线程数过多时系统开销巨大。现代高性能服务器采用 **事件驱动（Event-Driven）** 的方式：用操作系统提供的 I/O 多路复用机制（如 Linux 的 epoll）来同时监视大量连接，哪个连接有数据到达，就处理哪个连接。

本项目就是这种思路的实现，采用的是经典的 **Reactor 模式**。

---

## 核心概念解释

### Reactor 模式

Reactor 的字面意思是"反应器"。它的工作方式是：

1. 有一个事件循环（EventLoop），不断地询问操作系统："现在哪些文件描述符（fd）上有事件发生？"
2. 操作系统告诉它哪些 fd 就绪了（比如有数据可读、可以写入数据）
3. EventLoop 找到对应的处理器（Channel），调用预先注册的回调函数来处理事件

简单来说：**注册事件 → 等待事件 → 分发事件 → 处理事件**，如此循环。

### One Loop Per Thread（一个线程一个事件循环）

本项目的多线程模型是：

- **主线程（mainLoop）**：运行一个 EventLoop，专门负责监听新连接的到来（通过 Acceptor）
- **IO 线程（subLoop）**：每个 IO 线程运行一个独立的 EventLoop，负责处理已建立连接的读写事件

当一个新客户端连接进来时，主线程把它分配（round-robin 轮询）给某个 IO 线程，此后这个连接的所有操作都在那个 IO 线程中完成。

### 文件描述符（fd）

在 Linux 中，一切皆文件。网络连接本质上也是一个"文件"，用一个整数（文件描述符 fd）来标识。对网络连接的读写操作，就是对这个 fd 的 read/write 操作。

### epoll

epoll 是 Linux 提供的高效 I/O 多路复用机制。简单说就是：

- `epoll_create`：创建一个 epoll 实例
- `epoll_ctl`：告诉 epoll 你关注哪些 fd 上的哪些事件（读？写？）
- `epoll_wait`：阻塞等待，直到有事件发生，返回所有就绪的 fd

与 select/poll 相比，epoll 在大量连接时性能不会随连接数增长而下降。

### 非阻塞 I/O

普通的 read/write 调用在没有数据时会"阻塞"——卡住当前线程，直到有数据为止。这对事件驱动模型是不可接受的，因为一个线程要服务多个连接，不能被任何一个连接卡住。

所以本项目中所有 socket 都设为非阻塞模式（SOCK_NONBLOCK）：read/write 在没有数据时立即返回，而不是卡住。

---

## 架构概览

```
                         TcpServer
                        /    |    \
                       /     |     \
                Acceptor  EventLoopThreadPool  ConnectionMap
                  |            |
                  |      EventLoopThread (x N)
                  |            |
                  |       EventLoop (subLoop)
                  |            |
                  |        Poller (epoll)
                  |            |
        mainLoop的Poller    Channel (每个fd一个)
                  |
              Channel (listenfd)
                  
对于每个已建立的连接:
        TcpConnection
           /     \
        Socket   Channel ─── 注册到所属EventLoop的Poller
          |
        Buffer (输入/输出缓冲区)
```

---

## 一个连接的完整生命周期

以 echo 服务器为例，当客户端连接并发送数据时，发生了什么：

### 1. 启动阶段

```
main() 创建 EventLoop（mainLoop）和 TcpServer
  → TcpServer 创建 Acceptor（在 mainLoop 上监听端口）
  → TcpServer 创建 EventLoopThreadPool（启动 N 个 IO 线程）
  → 每个 IO 线程创建自己的 EventLoop（subLoop）并进入 loop() 循环
  → mainLoop 也进入 loop() 循环
```

### 2. 新连接到来

```
客户端 connect()
  → mainLoop 的 epoll_wait 检测到 listenfd 可读
  → Acceptor::handleRead() 被调用
  → accept4() 获得新连接的 connfd
  → TcpServer::newConnection() 被调用
  → 通过 round-robin 选择一个 subLoop
  → 创建 TcpConnection 对象（持有 connfd）
  → 在 subLoop 中注册 connfd 到 epoll，开始监听读事件
  → 调用用户的 ConnectionCallback（通知"连接建立"）
```

### 3. 数据到达

```
客户端 send("hello")
  → subLoop 的 epoll_wait 检测到 connfd 可读
  → TcpConnection::handleRead() 被调用
  → Buffer::readFd() 从 connfd 读取数据到输入缓冲区
  → 调用用户的 MessageCallback（传入 Buffer 供用户读取）
  → 用户调用 conn->send(msg) 回送数据
```

### 4. 发送数据

```
conn->send(msg)
  → 如果在当前 IO 线程中，直接调用 sendInLoop()
  → 否则通过 runInLoop() 转发到 IO 线程执行
  → sendInLoop(): 尝试直接 write()
    → 如果一次写完：触发 WriteCompleteCallback
    → 如果没写完：剩余数据存入 outputBuffer_，注册写事件
    → 之后 epoll 通知可写时，handleWrite() 继续发送
```

### 5. 连接断开

```
客户端关闭连接
  → epoll 检测到 connfd 可读（read 返回 0）
  → TcpConnection::handleClose()
  → 从 epoll 中移除该 fd
  → 调用用户的 ConnectionCallback（通知"连接断开"）
  → TcpServer::removeConnection() 清理连接资源
```

---

## 组件详解

### EventLoop —— 事件循环（Reactor 核心）

每个 EventLoop 运行在一个线程中，核心是一个无限循环：

```cpp
while (!quit_) {
    activeChannels = poller_->poll();   // 等待事件（epoll_wait）
    for (channel : activeChannels)
        channel->handleEvent();          // 分发处理就绪事件
    doPendingFunctors();                 // 执行其他线程投递过来的任务
}
```

**跨线程唤醒机制**：当其他线程想让某个 EventLoop 执行一个回调函数时，它会把回调放入队列，然后通过 eventfd 写入 8 个字节来唤醒该 EventLoop（让 epoll_wait 立即返回）。

### Channel —— fd 事件分发器

Channel 是 fd 和回调函数之间的桥梁。每个 fd 对应一个 Channel：

- 记录该 fd 关注的事件（EPOLLIN/EPOLLOUT）
- 记录 epoll 实际返回的事件（revents）
- 持有各类事件的回调函数（读回调、写回调、关闭回调、错误回调）
- 调用 `enableReading()`/`enableWriting()` 来修改关注的事件（内部调用 epoll_ctl）

**tie 机制**：Channel 用 weak_ptr 绑定其所有者（TcpConnection），在处理事件前先 lock() 检查所有者是否还存活，防止回调执行时对象已被销毁（use-after-free）。

### Poller / EPollPoller —— I/O 多路复用

Poller 是抽象基类，EPollPoller 是基于 epoll 的具体实现：

- `poll()`：调用 epoll_wait，返回就绪的 Channel 列表
- `updateChannel()`：调用 epoll_ctl 添加/修改/删除 fd 的事件监听
- `removeChannel()`：从 epoll 中移除 fd

每个 Channel 有三种状态（用 index_ 表示）：
- kNew (-1)：从未注册过
- kAdded (1)：已注册在 epoll 中
- kDeleted (2)：已从 epoll 删除（但还在 channels_ map 中）

### Buffer —— 应用层读写缓冲区

为什么需要缓冲区？因为：
- TCP 是字节流协议，一次 read 可能读到半个消息，也可能读到多个消息
- 非阻塞写入时，一次 write 可能只写出了部分数据

Buffer 的内存布局：

```
+-------------------+------------------+------------------+
|  prependable(8B)  |  readable bytes  |  writable bytes  |
+-------------------+------------------+------------------+
|                   |                  |                  |
0          readerIndex_       writerIndex_          buffer_.size()
```

- prependable：预留空间，可以在消息前面填入长度等协议头
- readable：已接收但还未被用户取走的数据
- writable：还能写入的空闲空间

**readFd() 的巧妙设计**：使用 readv() 分散读取——同时读入 buffer 的可写空间和一个 64KB 的栈缓冲区。这样一次系统调用就能读取大量数据，如果 buffer 不够大再动态扩容。

### TcpConnection —— TCP 连接

代表一个已建立的 TCP 连接，管理连接的整个生命周期。每个连接拥有：
- 一个 Socket（RAII 管理 fd）
- 一个 Channel（注册到所属 EventLoop 的 Poller 上）
- 输入/输出 Buffer

状态机：`kConnecting → kConnected → kDisconnecting → kDisconnected`

### TcpServer —— TCP 服务器

用户直接使用的顶层类，负责：
- 管理 Acceptor（监听新连接）
- 管理 EventLoopThreadPool（IO 线程池）
- 管理所有 TcpConnection（连接映射表）
- 向用户暴露回调接口（onConnection, onMessage, onWriteComplete）

### Acceptor —— 连接接受器

封装了 listen socket 的操作：
- 创建非阻塞 socket → bind → listen
- 当 listenfd 可读时，调用 accept4() 接受新连接
- 通知 TcpServer 有新连接到来

### EventLoopThreadPool —— IO 线程池

管理多个 EventLoopThread，每个 EventLoopThread 内部创建一个 EventLoop 并在独立线程中运行。新连接到来时，通过 `getNextLoop()` 以 round-robin 方式选择一个 subLoop。

---

## 构建和运行

### 环境要求

- Linux（使用了 epoll、eventfd、accept4 等 Linux 特有 API）
- GCC（支持 C++11）
- CMake 3.15+

### 编译

```bash
cd build
cmake ..
make
```

编译产物：
- `lib/libmuduo.so`：共享库
- `example/testserver`：echo 示例服务器

### 运行示例

```bash
# 终端 1：启动 echo 服务器
cd /path/to/muduo
LD_LIBRARY_PATH=./lib ./example/testserver

# 终端 2：用 nc 测试
echo "hello" | nc localhost 8080
```

---

## 使用示例

下面是一个完整的 echo 服务器（收到什么就回复什么）：

```cpp
#include <string>
#include "TcpServer.h"
#include "Logger.h"

class EchoServer
{
public:
    EchoServer(EventLoop* loop, const InetAddress& addr, const std::string& name)
        : server_(loop, addr, name)
        , loop_(loop)
    {
        // 设置连接建立/断开时的回调
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        
        // 设置收到消息时的回调
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        
        // 设置 IO 线程数量（0 表示只用 mainLoop，不创建额外线程）
        server_.setThreadNum(3);
    }

    void start() { server_.start(); }

private:
    // 连接状态变化时的回调
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected())
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
        else
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());
    }

    // 收到消息时的回调：读取全部数据并原样回送
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time)
    {
        std::string msg(buf->retrieveAllAsString());
        conn->send(msg);
    }

    TcpServer server_;
    EventLoop* loop_;
};

int main()
{
    EventLoop loop;              // 创建主事件循环
    InetAddress addr(8080);      // 监听端口 8080
    
    EchoServer server(&loop, addr, "EchoServer");
    server.start();              // 启动监听和 IO 线程
    loop.loop();                 // 进入事件循环（永不返回，除非 quit）

    return 0;
}
```

---

## 目录结构

```
muduo/
├── include/          公共头文件（所有 .h）
├── src/              实现文件（所有 .cpp）
├── example/          示例程序
├── lib/              编译产物（libmuduo.so）
├── build/            CMake 构建目录
└── CMakeLists.txt    顶层 CMake 文件
```

---

## 设计特点

- **零第三方依赖**：仅使用 C++ 标准库 + Linux 系统调用 + pthreads
- **基于回调的编程模型**：用户设置回调函数，由框架在合适的时机调用
- **线程安全的跨线程调用**：通过 `runInLoop` + eventfd 唤醒机制安全地在线程间传递任务
- **智能指针管理连接生命周期**：TcpConnection 使用 shared_ptr 管理，配合 Channel 的 tie 机制防止 use-after-free
- **非阻塞 + 应用层缓冲区**：所有 socket 非阻塞，读写数据通过 Buffer 缓冲，应对 TCP 粘包和部分写入
