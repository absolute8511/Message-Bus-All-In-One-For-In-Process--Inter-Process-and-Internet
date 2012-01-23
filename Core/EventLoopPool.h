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
    static bool  InitEventLoopPool();
    static void  DestroyEventLoopPool();
    static bool CreateEventLoop(const std::string& name, boost::shared_ptr<SockWaiterBase> spwaiter);
    static void TerminateLoop(const std::string& name);
    static boost::shared_ptr< EventLoop > GetEventLoop(const std::string& name);

    //boost::shared_ptr< EventLoop > GetInnerEventLoop(TcpSockSmartPtr sp_tcp);
    static bool AddTcpSockToInnerLoop(TcpSockSmartPtr sp_tcp);

private:
    EventLoopPool();
    ~EventLoopPool();
};


} }
#endif
