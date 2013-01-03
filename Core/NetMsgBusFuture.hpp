#ifndef  NETMSGBUS_FUTURE_H
#define  NETMSGBUS_FUTURE_H

#include "condition.hpp"
#include <errno.h>
#include <string>
#include <time.h>
#include <sys/time.h>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/unordered_map.hpp>

using std::string;

namespace NetMsgBus
{

class NetFuture: public boost::enable_shared_from_this<NetFuture>
{
public:
    typedef boost::function<void(const NetFuture&)> futureCB;
    void set_result(const char* pdata, size_t len)
    {
        core::common::locker_guard guard(wait_lock_);
        rsp_content_.assign(pdata, pdata + len);
        ready_ = true;
        if (callback_)
            callback_(*this);
        wait_cond_.notify_all();
    }

    void set_result(const std::string& result)
    {
        set_result(result.data(), result.size());
    }

    void set_error(const std::string& errinfo)
    {
        has_error_ = true;
        set_result(errinfo.data(), errinfo.size());
    }

    bool is_bad() const
    {
        return time(NULL) - generated_time_ > MAX_EXIST_TIME;
    }

    bool has_err() const
    {
        return has_error_;
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

    bool get(std::string& result) const
    {
        if(!ready_)
            return false;
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
    NetFuture(futureCB cb = NULL)
        :ready_(false),
        has_error_(false),
        generated_time_(time(NULL)),
        callback_(cb)
    {
    }

private:
    bool ready_;
    bool has_error_;
    std::string rsp_content_;
    time_t  generated_time_;
    futureCB callback_;
    core::common::locker wait_lock_;
    core::common::condition wait_cond_;
    static const int MAX_EXIST_TIME = 120;
};

class FutureMgr
{
private:
    uint32_t  future_inc_id_;
    core::common::locker    rsp_sendmsg_lock_;
    typedef boost::unordered_map< uint32_t, boost::shared_ptr<NetFuture> > FutureRspContainerT;
    FutureRspContainerT sendmsg_rsp_container_;

public:
    FutureMgr()
        :future_inc_id_(0)
    {
        sendmsg_rsp_container_.max_load_factor(0.8);
    }
    std::pair<uint32_t, boost::shared_ptr<NetFuture> > safe_insert_future(NetFuture::futureCB callback = NULL)
    {
        core::common::locker_guard guard(rsp_sendmsg_lock_);
        ++future_inc_id_;
        if(future_inc_id_ == 0)
            ++future_inc_id_;
        uint32_t waiting_futureid = future_inc_id_;
        std::pair<FutureRspContainerT::iterator, bool> inserted = sendmsg_rsp_container_.insert(
            std::make_pair(waiting_futureid, boost::shared_ptr<NetFuture>(new NetFuture(callback))));
        assert(inserted.second);
        return std::make_pair(waiting_futureid, inserted.first->second);
    }

    boost::shared_ptr<NetFuture> safe_get_future(uint32_t future_sid, bool erase = false)
    {
        boost::shared_ptr<NetFuture> future;
        core::common::locker_guard guard(rsp_sendmsg_lock_);
        FutureRspContainerT::iterator future_it = sendmsg_rsp_container_.find(future_sid);
        if(future_it != sendmsg_rsp_container_.end())
        {
            future = future_it->second;
            if (erase)
                sendmsg_rsp_container_.erase(future_it);
        }
        return future;
    }
    void safe_remove_future(uint32_t future_sid)
    {
        core::common::locker_guard guard(rsp_sendmsg_lock_);
        sendmsg_rsp_container_.erase(future_sid);
    }

    void safe_clear_bad_future()
    {
        core::common::locker_guard guard(rsp_sendmsg_lock_);
        FutureRspContainerT::iterator future_it = sendmsg_rsp_container_.begin();
        while(future_it != sendmsg_rsp_container_.end())
        {
            if(future_it->second && future_it->second->is_bad())
            {
                future_it = sendmsg_rsp_container_.erase(future_it);
            }
            else
            {
                ++future_it;
            }
        }
    }


};

}
#endif // end of NETMSGBUS_FUTURE_H
