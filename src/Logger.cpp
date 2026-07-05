#include "Logger.h"
#include "Timestamp.h"

#include <iostream>

// Meyers' Singleton：利用 C++11 保证的局部静态变量线程安全初始化
// 第一次调用 instance() 时创建 Logger 对象，之后每次返回同一个实例
Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::setLogLevel(LogLevel level)
{
    logLevel_ = level;
}

// 输出日志：[级别]时间戳 : 消息
void Logger::log(std::string msg)
{
    std::string pre = "";
    switch(logLevel_)
    {
        case INFO:
            pre = "[INFO]";
            break;
        case ERROR:
            pre = "[ERROR]";
            break;
        case FATAL:
            pre = "[FATAL]";
            break;
        case DEBUG:
            pre = "[DEBUG]";
            break;
        default:
            break;
    }

    // 格式：[INFO]2024/01/15 14:30:00 : some message
    std::cout << pre + Timestamp::now().toString() + " : " + msg << std::endl;
}
