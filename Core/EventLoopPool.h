#ifndef CORE_NET_EVENTLOOPPOOL_H
#define CORE_NET_EVENTLOOPPOOL_H

#include "EventLoop.h"
#include "lock.hpp"
#include <map>
#include <string>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <pthread.h>

namespace core { namespace net {

class EventLoopPool : private boost::noncopyable
{
public:
    EventLoopPool();
    ~EventLoopPool();
    bool CreateEventLoop(const std::string& name, boost::shared_ptr<SockWaiterBase> spwaiter);
    //bool AddTcpSockToLoop(const std::string& name, TcpSockSmartPtr sp_tcp);
    void TerminateLoop(const std::string& name);
    boost::shared_ptr< EventLoop > GetEventLoop(const std::string& name);

private:
    struct EventLoopWrapper
    {
        boost::shared_ptr< EventLoop > eventloop;
        pthread_t looptid;
    };
    typedef std::map<std::string, EventLoopWrapper > EventLoopContainerT;
    EventLoopContainerT  m_eventloop_pool;
    core::common::locker m_pool_locker;

};

} }
#endif
