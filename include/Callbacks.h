#pragma once

#include <memory>
#include <functional>

class Buffer;
class TcpConnection;
class Timestamp;

/**
 * Callbacks - 全局回调类型定义
 *
 * 这些 typedef 定义了用户与网络库交互的回调函数签名。
 * 用户在使用 TcpServer 时，通过设置这些回调来实现业务逻辑。
 *
 * TcpConnectionPtr 使用 shared_ptr 管理 TcpConnection 的生命周期，
 * 确保在回调执行期间连接对象不会被销毁。
 */

// TcpConnection 的共享指针，所有地方都用这个类型引用连接
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

// 连接建立/断开时触发的回调
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;

// 连接关闭时的内部回调（由 TcpServer 设置，用户不直接使用）
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;

// 数据全部写完（输出缓冲区清空）后触发的回调
// 用于实现"发完一条消息后再发下一条"的流控逻辑
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;

// 输出缓冲区水位超过阈值时触发的回调
// 用于防止发送速度远大于对端接收速度时内存无限增长
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr&, size_t)>;

// 收到消息时触发的回调
// 参数：连接指针、输入缓冲区（包含收到的数据）、消息到达时间
using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*, Timestamp)>;
