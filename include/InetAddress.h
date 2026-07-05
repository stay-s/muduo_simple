#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>

/**
 * InetAddress - 网络地址封装
 *
 * 封装了 sockaddr_in 结构体，提供 IP:Port 的友好操作接口。
 *
 * sockaddr_in 是 Linux 网络编程中表示 IPv4 地址的标准结构体，包含：
 * - sin_family: 地址族（AF_INET 表示 IPv4）
 * - sin_port: 端口号（网络字节序，需用 htons 转换）
 * - sin_addr: IP 地址（网络字节序）
 *
 * 网络字节序 vs 主机字节序：
 * 网络协议规定数据用大端序传输（高位字节在前），而 x86 CPU 使用小端序。
 * htons/ntohs 负责在两者间转换（host to network short / network to host short）。
 */
class InetAddress
{
public:
    // 构造函数：指定端口和 IP（默认 127.0.0.1 即本机回环地址）
    explicit InetAddress(uint16_t port = 0, std::string ip = "127.0.0.1");

    // 从已有的 sockaddr_in 构造（accept 获得对端地址时使用）
    explicit InetAddress(const sockaddr_in &addr)
        : addr_(addr)
    {
    }

    std::string toIp() const;      // 返回 IP 字符串，如 "192.168.1.1"
    std::string toIpPort() const;  // 返回 "IP:Port" 字符串，如 "192.168.1.1:8080"
    uint16_t toPort() const;       // 返回端口号（主机字节序）

    const sockaddr_in *getSockAddr() const { return &addr_;}
    void setSockAddr(const sockaddr_in &addr) { addr_ = addr;}

private:
    sockaddr_in addr_;  // 底层使用的 IPv4 地址结构
};
