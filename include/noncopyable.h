#pragma once

/**
 * noncopyable - 不可拷贝基类
 *
 * 通过将拷贝构造和拷贝赋值声明为 delete，禁止派生类的拷贝操作。
 * 使用方式：class MyClass : noncopyable { ... };（私有继承）
 *
 * 为什么需要这个？
 * 像 EventLoop、TcpConnection 这样的对象持有系统资源（fd、线程等），
 * 拷贝它们没有意义且会导致资源重复释放的 bug。
 * 通过继承 noncopyable，在编译期就禁止拷贝，避免误用。
 */
class noncopyable
{
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator=(const noncopyable&) = delete;

protected:
    noncopyable() = default;
    ~noncopyable() = default;
};
