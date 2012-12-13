#ifndef  NETMSGBUS_FUTURE_H
#define  NETMSGBUS_FUTURE_H

#include "condition.hpp"
#include <errno.h>
#include <string>
#include <time.h>
#include <sys/time.h>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

using std::string;
using namespace core::net;

namespace NetMsgBus
{

class NetFuture: public boost::enable_shared_from_this<NetFuture>
{
public:
    void set_result(const char* pdata, size_t len)
    {
        core::common::locker_guard guard(wait_lock_);
        rsp_content_.assign(pdata, pdata + len);
        ready_ = true;
        wait_cond_.notify_all();
    }

    void set_result(const std::string& result)
    {
        set_result(result.data(), result.size());
    }

    bool is_bad() const
    {
        return time(NULL) - generated_time_ > MAX_EXIST_TIME;
    }

    bool get(int timeout, std::string& result)
    {
        if(!join(timeout))
        {
            result = "wait time out.";
            return false;
        }
        result = rsp_content_;
        return true;
    }

    bool join(int timeout)
    {
        struct timespec ts;
        ts.tv_sec = time(NULL) + timeout;
        ts.tv_nsec = 0;
        bool ready = false;
        while(!ready)
        {
            {
                core::common::locker_guard guard(wait_lock_);
                ready = ready_;
                if(ready)
                {
                    break;
                }
                int retcode = wait_cond_.waittime(wait_lock_, &ts);
                if(retcode == ETIMEDOUT)
                {
                    //false;
                    break;
                }
                ready = ready_;
                if(ready)
                {
                    break;
                }
            }
        }
        return ready;
    }
    NetFuture()
        :ready_(false),
        generated_time_(time(NULL))
    {
    }

private:
    bool ready_;
    std::string rsp_content_;
    time_t  generated_time_;
    core::common::locker wait_lock_;
    core::common::condition wait_cond_;
    static const int MAX_EXIST_TIME = 120;
};

}
#endif // end of NETMSGBUS_FUTURE_H
