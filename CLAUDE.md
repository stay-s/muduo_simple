# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Build the project (out-of-source)
cd build && cmake .. && make

# The shared library outputs to lib/
# The example binary outputs to example/testserver
```

There is no test framework or CI. The only runnable target is the example echo server (`example/testserver`, listens on port 8080).

## Architecture

This is a simplified reimplementation of Chen Shuo's muduo networking library, following the **Reactor pattern** with a "one loop per thread" design. Linux-only (epoll, eventfd, accept4, SYS_gettid).

### Layer Structure

**Event-driven core (Reactor):**
- `EventLoop` — drives epoll_wait → handleEvent → doPendingFunctors; uses eventfd for cross-thread wakeup; enforces one-loop-per-thread via `__thread` guard
- `Channel` — bridges an fd + event mask to callbacks; dispatches EPOLLIN/OUT/HUP/ERR; uses weak_ptr tie for lifetime safety
- `EPollPoller` (implements `Poller`) — wraps epoll_create1/epoll_wait/epoll_ctl; stores Channel* in epoll_event.data.ptr

**TCP application layer:**
- `TcpServer` → owns `Acceptor` + `EventLoopThreadPool` + connection map
- `EventLoopThreadPool` → round-robin pool of `EventLoopThread` (each binds one EventLoop to one std::thread)
- `TcpConnection` — per-connection state machine (kConnecting/kConnected/kDisconnecting/kDisconnected); owns Socket + Channel + input/output Buffers; supports send, sendFile, shutdown

**Utilities:**
- `Buffer` — three-segment (prepend/readable/writable) with readv scatter-read
- `InetAddress`, `Socket` (RAII fd), `Thread` (semaphore-based start sync), `Logger` (singleton, level macros), `Timestamp`, `CurrentThread`

### Key Design Decisions

- All public headers are in `include/`, all implementations in `src/`
- Zero third-party dependencies — only C++ stdlib + POSIX/Linux APIs + pthreads
- CMake requires 3.15+, sets C++11 standard
- Callback types defined in `Callbacks.h` (ConnectionCallback, MessageCallback, WriteCompleteCallback, HighWaterMarkCallback, CloseCallback)
- `noncopyable` base class used via private inheritance throughout
