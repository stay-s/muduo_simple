#pragma once

#include "noncopyable.h"

#include <string>

/**
 * Logger - 日志系统
 *
 * 使用 Meyers' Singleton 模式（静态局部变量实现的单例），全局唯一。
 * 提供四个日志级别的宏：LOG_INFO、LOG_ERROR、LOG_FATAL、LOG_DEBUG。
 *
 * 使用方式：
 *   LOG_INFO("accept new connection from %s", addr.c_str());
 *   LOG_FATAL("bindAddress fail");  // FATAL 会输出日志后终止程序（通过 exit）
 *
 * LOG_DEBUG 默认不输出，需要编译时定义 MUDEBUG 宏才生效：
 *   cmake -DCMAKE_CXX_FLAGS="-DMUDEBUG" ..
 *
 * 每条日志格式：[级别]YYYY/MM/DD HH:MM:SS : 消息内容
 */

// 日志宏使用 do { ... } while(0) 包裹，这是 C/C++ 宏的标准写法，
// 保证宏在 if/else 等语句中行为正确，不会因为缺少大括号而出 bug。
#define LOG_INFO(logmsgFormat, ...)                         \
    do                                                      \
    {                                                       \
        Logger &logger = Logger::instance();                \
        logger.setLogLevel(INFO);                           \
        char buf[1024] = {0};                               \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);   \
        logger.log(buf);                                    \
    } while (0)

#define LOG_ERROR(logmsgFormat, ...)                        \
    do                                                      \
    {                                                       \
        Logger &logger = Logger::instance();                \
        logger.setLogLevel(ERROR);                          \
        char buf[1024] = {0};                               \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);   \
        logger.log(buf);                                    \
    } while (0)

#define LOG_FATAL(logmsgFormat, ...)                        \
    do                                                      \
    {                                                       \
        Logger &logger = Logger::instance();                \
        logger.setLogLevel(FATAL);                          \
        char buf[1024] = {0};                               \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);   \
        logger.log(buf);                                    \
    } while (0)

#ifdef MUDEBUG
#define LOG_DEBUG(logmsgFormat, ...)                        \
    do                                                      \
    {                                                       \
        Logger &logger = Logger::instance();                \
        logger.setLogLevel(DEBUG);                          \
        char buf[1024] = {0};                               \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__);   \
        logger.log(buf);                                    \
    } while (0)
#else
#define LOG_DEBUG(logmsgFormat, ...)
#endif

// 日志级别枚举
enum LogLevel
{
    INFO,   // 普通信息
    ERROR,  // 错误（可恢复，程序继续运行）
    FATAL,  // 致命错误（程序无法继续，输出后退出）
    DEBUG,  // 调试信息（默认不输出）
};

class Logger : public noncopyable
{
public:
    // 获取全局唯一的 Logger 实例（Meyers' Singleton）
    static Logger &instance();

    void setLogLevel(LogLevel level);

    // 输出一条日志消息
    void log(std::string msg);

private:
    LogLevel logLevel_;
};
