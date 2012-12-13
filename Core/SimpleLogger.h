#ifndef CORE_SIMPLE_LOGGER_H
#define CORE_SIMPLE_LOGGER_H

#include "lock.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <vector>

using std::string;

namespace core{
enum LogLevel
{
    lv_error,
    lv_warn,
    lv_info,
    lv_debug,
};

struct LogInfo
{
    string category_;
    LogLevel level_;
    string content_;
    string time_;
    LogInfo(const string& cat, LogLevel level, const string& content, const string& time)
    {
        category_ = cat;
        level_ = level;
        content_ = content;
        time_ = time;
    }
};

class SimpleLogger
{
public:
    static SimpleLogger& Instance();
    bool Init(const std::string& logfilepath, LogLevel global_lv);
    void Reset();
    void queue_log(const LogInfo& loginfo);
    void flush_all();
    ~SimpleLogger();
    LogLevel GetLogLevel();
private:
    SimpleLogger();
    void write2file();
    FILE* m_logfile;
    std::vector<LogInfo> m_waiting_logs;
    core::common::locker m_lock;
    std::string  m_level_str[4];
    LogLevel  m_global_lv;
};

class LoggerCategory
{
public:
    LoggerCategory(const std::string& category);
    void Log(LogLevel lv, const char* fmt, ...);
private:
    std::string m_category;
};

#define LOG(logger, level, fmt, ...) logger.Log(level, (std::string("[%s:%d]") + fmt).c_str(), __FILE__, __LINE__, ##__VA_ARGS__)
}// end of namespace core
#endif
