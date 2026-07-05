#include "Timestamp.h"

#include <time.h>

Timestamp::Timestamp() : microSecondsSinceEpoch_(0)
{
}

Timestamp::Timestamp(int64_t microSecondsSinceEpoch) : microSecondsSinceEpoch_(microSecondsSinceEpoch)
{
}

// 获取当前时间（秒级精度）
Timestamp Timestamp::now()
{
    return Timestamp(time(NULL));  // time(NULL) 返回自 1970-01-01 以来的秒数
}

// 格式化为可读时间字符串："YYYY/MM/DD HH:MM:SS"
std::string Timestamp::toString() const
{
    char buf[128] = {0};
    // localtime：将 time_t 转为本地时间的 tm 结构体
    tm *tm_time = localtime(&microSecondsSinceEpoch_);
    snprintf(buf, 128, "%4d/%02d/%02d %02d:%02d:%02d",
             tm_time->tm_year + 1900,   // tm_year 是从 1900 年开始的偏移量
             tm_time->tm_mon + 1,       // tm_mon 范围 0-11
             tm_time->tm_mday,
             tm_time->tm_hour,
             tm_time->tm_min,
             tm_time->tm_sec
            );

    return buf;
}
