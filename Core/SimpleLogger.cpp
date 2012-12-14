#include "SimpleLogger.h"
#include "lock.hpp"
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <string>
#include <vector>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <errno.h>

using std::string;

namespace core{

SimpleLogger& SimpleLogger::Instance()
{
    static SimpleLogger static_logsaver;
    return static_logsaver;
}

bool SimpleLogger::Init(const std::string& logfilepath, LogLevel global_lv)
{
    m_logfile = fopen(logfilepath.c_str(), "a");
    if(m_logfile == NULL)
    {
        perror("open file for writing log failed.");
        return false;
    }
    chmod(logfilepath.c_str(), S_IRUSR|S_IWUSR|S_IROTH|S_IWOTH);
    m_level_str[0] = "ERROR";
    m_level_str[1] = "WARN";
    m_level_str[2] = "INFO";
    m_level_str[3] = "DEBUG";
    m_global_lv = global_lv;
    return true;
}
void SimpleLogger::Reset()
{
    write2file();
    if(m_logfile != NULL)
    {
        fclose(m_logfile);
        m_logfile = NULL;
    }
    core::common::locker_guard guard(m_lock);
    m_waiting_logs.clear();
}
void SimpleLogger::queue_log(const LogInfo& loginfo)
{
    if(m_logfile == NULL)
        return;
    core::common::locker_guard guard(m_lock);
    m_waiting_logs.push_back(loginfo);
    threadpool::queue_timer_task(boost::bind(&SimpleLogger::flush_all, this), 2, false);
}
void SimpleLogger::flush_all()
{
    threadpool::queue_work_task_to_named_thread(boost::bind(&SimpleLogger::write2file, this), "logsaver");
}
SimpleLogger::~SimpleLogger()
{
    Reset(); 
}
LogLevel SimpleLogger::GetLogLevel()
{
    return m_global_lv;
}
SimpleLogger::SimpleLogger()
{
    m_logfile = NULL;
    m_global_lv = lv_warn;
}
void SimpleLogger::write2file()
{
    // save im msg
    if(m_logfile == NULL)
        return;
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence =  SEEK_SET;
    fl.l_start =  0;
    fl.l_len = 0;
    fl.l_pid = getpid();
    while(fcntl(fileno(m_logfile), F_SETLKW, &fl) == -1)
    {
        perror("fcntl get lock error.");
        if(errno != EINTR)
            return;
    }
    std::vector<LogInfo> writinglogs;
    {
        core::common::locker_guard guard(m_lock);
        writinglogs.swap(m_waiting_logs);
    }
    for(size_t i = 0; i < writinglogs.size(); i++)
    {
        LogInfo& info = writinglogs[i];
        fprintf(m_logfile, "%s in %s : %s [%s]\n", m_level_str[info.level_].c_str(),
            info.category_.c_str(), info.content_.c_str(), info.time_.c_str());
    }
    fflush(m_logfile);
    fl.l_type = F_UNLCK;
    if(fcntl(fileno(m_logfile), F_SETLK, &fl) == -1)
    {
        perror("fcntl release lock error.");
        assert(false);
    }
}

LoggerCategory::LoggerCategory(const std::string& category)
{
    m_category = category;
    assert(m_category.size() < 64);
}
void LoggerCategory::Log(LogLevel lv, const char* fmt, ...)
{
    string errcodestr;
    if(lv == lv_error)
    {
        char err[6];
        memset(err, 0, 6);
        snprintf(err, 6, "%d", errno);
        errcodestr = "error code: " + string(err) + ".";
        perror(NULL);
    }
    if(lv > SimpleLogger::Instance().GetLogLevel())
        return;
    {
#ifdef NDEBUG 
        if(lv == lv_debug)
            return;
#endif
        va_list vl_args;
        va_start(vl_args, fmt);
        vprintf(fmt, vl_args);
        va_end(vl_args);
        printf("\n");
    }
    va_list vl_args;
    char* buffer;
    char* tmpbuf;
    int size = 32;
    int retlen;
    if( (buffer = (char*)malloc(size)) == NULL )
        return;
    while(1)
    {
        va_start(vl_args, fmt);
        retlen = vsnprintf(buffer, size, fmt, vl_args);
        va_end(vl_args);
        if(retlen > -1 && retlen < size)
            break;
        if(retlen > -1)
            size = retlen + 1;
        else
            size *= 2;
        if( (tmpbuf = (char*)realloc(buffer, size)) == NULL )
        {
            free(buffer);
            return;
        }
        else
        {
            buffer = tmpbuf;
        }
    }
    time_t tnow = time(NULL);
    string timestr(ctime(&tnow));
    timestr[timestr.size() - 1] = '\0';
    LogInfo info(m_category, lv, errcodestr + string(buffer, retlen), timestr);
    SimpleLogger::Instance().queue_log(info);
    free(buffer);
}

}// end of namespace core
