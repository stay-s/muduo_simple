#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <stddef.h>

/**
 * Buffer - 应用层读写缓冲区
 *
 * 为什么需要 Buffer？
 * 1. TCP 是字节流协议，没有消息边界。一次 read 可能读到半个消息（粘包），
 *    也可能读到多个消息，需要缓冲区来累积数据直到凑齐一个完整消息。
 * 2. 非阻塞 write 可能一次只写出部分数据（内核发送缓冲区满了），
 *    剩余数据需要暂存在输出缓冲区中，等可写时继续发送。
 *
 * 内存布局（三段式）：
 * +-------------------+------------------+------------------+
 * |   prependable     |  readable bytes  |  writable bytes  |
 * |   （预留头部）     |  （可读数据）     |  （可写空间）     |
 * +-------------------+------------------+------------------+
 * |                   |                  |                  |
 * 0        readerIndex_        writerIndex_          buffer_.size()
 *
 * - prependable（默认8字节）：预留空间，用于在消息前面填入长度等协议头，
 *   避免整体数据的搬移。
 * - readable：已经从 socket 读入但还未被用户消费的数据。
 * - writable：buffer 中还能写入新数据的空闲空间。
 *
 * 空间不足时的策略（makeSpace）：
 * 1. 如果 prependable + writable 总空间够用，就把 readable 数据移到前面
 * 2. 如果总空间都不够，直接 resize 扩容
 */
class Buffer
{
public:
    static const size_t kCheapPrepend = 8;    // 预留的头部空间大小
    static const size_t kInitialSize = 1024;  // 初始缓冲区大小

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize), readerIndex_(kCheapPrepend), writerIndex_(kCheapPrepend)
    {
    }

    // 获取各区域的大小
    size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    size_t prependableBytes() const { return readerIndex_; }

    // 返回可读数据的起始地址
    const char *peek() const { return begin() + readerIndex_; }

    // 标记已消费 len 字节的数据（移动 readerIndex）
    void retrieve(size_t len)
    {
        if (len < readableBytes())
        {
            readerIndex_ += len;
        }
        else
        {
            retrieveAll();
        }
    }

    // 重置缓冲区（所有数据视为已消费）
    void retrieveAll()
    {
        readerIndex_ = kCheapPrepend;
        writerIndex_ = kCheapPrepend;
    }

    // 把缓冲区中的所有可读数据取出为 string
    std::string retrieveAllAsString() { return retrieveAsString(readableBytes());}
    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(), len);
        retrieve(len);  // 取出后要更新读指针
        return result;
    }

    // 确保有足够的可写空间（不够就扩容或整理空间）
    void ensureWritableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len);
        }
    }

    // 向缓冲区写入数据
    void append(const char *data, size_t len)
    {
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;
    }
    char *beginWrite() { return begin() + writerIndex_; }
    const char *beginWrite() const { return begin() + writerIndex_; }

    // 从 fd 读取数据到缓冲区（使用 readv 分散读取提高效率）
    ssize_t readFd(int fd, int *saveErrno);
    // 从缓冲区写出数据到 fd
    ssize_t writeFd(int fd, int *saveErrno);

private:
    char *begin() { return &*buffer_.begin(); }
    const char *begin() const { return &*buffer_.begin(); }

    // 空间整理/扩容策略
    void makeSpace(size_t len)
    {
        if (writableBytes() + prependableBytes() < len + kCheapPrepend)
        {
            // 总空间不够，只能扩容
            buffer_.resize(writerIndex_ + len);
        }
        else
        {
            // 把 readable 数据搬到前面，腾出尾部空间
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_,
                      begin() + writerIndex_,
                      begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
    }

private:
    std::vector<char> buffer_;  // 底层存储，用 vector 自动管理内存
    size_t readerIndex_;        // 读指针：下一个可读字节的位置
    size_t writerIndex_;        // 写指针：下一个可写字节的位置
};
