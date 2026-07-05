#pragma once

#include <iostream>
#include <string>

/**
 * Timestamp - 时间戳类
 *
 * 封装了一个基于 Unix 纪元（1970年1月1日）的时间戳。
 * 主要用于记录事件发生的时间（如消息到达时间），
 * 以及在日志中输出可读的时间字符串。
 */
class Timestamp
{
public:
    Timestamp();

    // explicit 防止 int64_t 隐式转换为 Timestamp
    explicit Timestamp(int64_t microSecondsSinceEpoch);

    // 获取当前时间的 Timestamp
    static Timestamp now();

    // 格式化为 "YYYY/MM/DD HH:MM:SS" 字符串
    std::string toString() const;

private:
    int64_t microSecondsSinceEpoch_;  // 自1970年以来的秒数
};
