#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

/**
 * 从 fd 读取数据到缓冲区
 *
 * 难点：不知道 TCP 数据有多少，buffer 可能不够大。
 * 解决方案：使用 readv（分散读取），同时读入两块内存：
 *   1. buffer 的 writable 空间
 *   2. 栈上的 64KB 临时空间（extrabuf）
 *
 * 好处：
 * - 只做一次系统调用就能读取大量数据（减少 syscall 开销）
 * - 如果数据量小（小于 buffer writable），直接读入 buffer，零拷贝
 * - 如果数据量大（超过 buffer writable），多出的部分读到栈上，再 append 到 buffer
 *   此时 buffer 会自动扩容
 *
 * 为什么用栈空间而不是直接给 buffer 预分配很大的空间？
 * 因为每个连接都有自己的 buffer，如果预分配很大会浪费内存。
 * 栈上的 64KB 是临时的，函数返回就释放了，不占用堆内存。
 *
 * Poller 工作在 LT（水平触发）模式，所以即使一次没读完也没关系，
 * 下次 epoll_wait 还会通知可读。
 */
ssize_t Buffer::readFd(int fd, int *saveErrno)
{
    // 栈上的临时缓冲区：当 buffer 空间不够时暂存溢出的数据
    char extrabuf[65536] = {0};

    // 设置两块 IO 向量：第一块是 buffer 的可写区域，第二块是栈上 extrabuf
    struct iovec vec[2];
    const size_t writable = writableBytes();

    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 如果 buffer 可写空间小于 64KB，就用两块（buffer + extrabuf）
    // 如果 buffer 可写空间已经 >= 64KB，一块就够了
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;

    // readv：分散读取，一次系统调用同时读入多块内存
    const ssize_t n = ::readv(fd, vec, iovcnt);

    if(n < 0)
    {
        *saveErrno = errno;
    }
    else if(n <= writable)
    {
        // 数据全部读到了 buffer 中，直接移动写指针
        writerIndex_ += n;
    }
    else
    {
        // 数据超出了 buffer 可写空间，多出的部分在 extrabuf 中
        writerIndex_ = buffer_.size();
        // 把 extrabuf 中的数据追加到 buffer（会触发扩容）
        append(extrabuf, n - writable);
    }
    return n;
}

// 从缓冲区写出数据到 fd
ssize_t Buffer::writeFd(int fd, int *saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if(n < 0)
    {
        *saveErrno = errno;
    }
    return n;
}
