#include "InetAddress.h"

#include <string.h>
#include <strings.h>

// 根据端口号和 IP 字符串构造 sockaddr_in
InetAddress::InetAddress(uint16_t port, std::string ip)
{
    ::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;               // IPv4 地址族
    addr_.sin_port = ::htons(port);           // 端口：主机字节序 → 网络字节序
    addr_.sin_addr.s_addr = ::inet_addr(ip.c_str());  // IP 字符串 → 网络字节序整数
}

// 将网络字节序的 IP 地址转换为可读字符串
std::string InetAddress::toIp() const
{
    char buf[64] = {0};
    // inet_ntop：network to presentation，将二进制 IP 转为字符串
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
    return buf;
}

// 返回 "IP:Port" 格式的字符串
std::string InetAddress::toIpPort() const
{
    char buf[64] = {0};
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
    size_t end = ::strlen(buf);
    uint16_t port = ::ntohs(addr_.sin_port);  // 网络字节序 → 主机字节序
    sprintf(buf + end, ":%u", port);
    return buf;
}

// 返回端口号（主机字节序）
uint16_t InetAddress::toPort() const
{
    return ::ntohs(addr_.sin_port);
}
