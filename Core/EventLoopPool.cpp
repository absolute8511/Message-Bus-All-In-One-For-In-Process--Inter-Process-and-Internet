#include "EventLoopPool.h"
#include "SimpleLogger.h"
#include <pthread.h>
#include <boost/shared_ptr.hpp>
#include <vector>
#include <stdio.h>

namespace core { namespace net {

static LoggerCategory g_log("EventLoopPool");

EventLoopPool::EventLoopPool()
{
}
EventLoopPool::~EventLoopPool()
{
    EventLoopContainerT::iterator it = m_eventloop_pool.begin();
    std::vector< pthread_t > jointids;
    while(it != m_eventloop_pool.end())
    {
        g_log.Log(lv_debug, "you should TerminateLoop :%s before exit.", it->first.c_str());
        it->second.eventloop->TerminateLoop();
        jointids.push_back(it->second.looptid);
        ++it;
    }
    for(size_t i = 0; i < jointids.size(); ++i)
        pthread_join(jointids[i], NULL);
    // wait for the event loop to end, because the loop has to use the eventloop object,
    // so we must clear the event loop after the loop finished.
    m_eventloop_pool.clear();
}
bool EventLoopPool::CreateEventLoop(const std::string& name, boost::shared_ptr<SockWaiterBase> spwaiter)
{
    core::common::locker_guard guard(m_pool_locker);
    if(m_eventloop_pool.find(name) != m_eventloop_pool.end())
    {// already created, just return true to indicate sucess.
        //m_eventloop_pool[name].eventloop->SetSockWaiter(spwaiter);
        return true;
    }
    boost::shared_ptr<EventLoop> sock_el(new EventLoop);
    sock_el->SetSockWaiter(spwaiter);
    pthread_t tid;
    if (sock_el->StartLoop(tid))
    {
        EventLoopWrapper wrapper;
        wrapper.eventloop = sock_el;
        wrapper.looptid = tid;
        m_eventloop_pool[name] = wrapper;
        return true;
    }
    g_log.Log(lv_warn, "event loop : %s failed to start.", name.c_str());
    return false;
}

//bool EventLoopPool::AddTcpSockToLoop(const std::string& name, TcpSockSmartPtr sp_tcp)
//{
//    core::common::locker_guard guard(m_pool_locker);
//    EventLoopContainerT::iterator it = m_eventloop_pool.find(name);
//    if(it != m_eventloop_pool.end())
//    {
//        it->second.eventloop->AddTcpSock(sp_tcp);
//        return true;
//    }
//    return false;
//}

boost::shared_ptr<EventLoop> EventLoopPool::GetEventLoop(const std::string& name)
{
    {
        core::common::locker_guard guard(m_pool_locker);
        EventLoopContainerT::iterator it = m_eventloop_pool.find(name);
        if(it != m_eventloop_pool.end())
        {
            return it->second.eventloop;
        }
    }
    return boost::shared_ptr< EventLoop >();
}

void EventLoopPool::TerminateLoop(const std::string& name)
{
    EventLoopWrapper wrapper;
    bool isexist = false;
    {
        core::common::locker_guard guard(m_pool_locker);
        EventLoopContainerT::iterator it = m_eventloop_pool.find(name);
        if(it != m_eventloop_pool.end())
        {
            isexist = true;
            wrapper.eventloop = it->second.eventloop;
            wrapper.looptid  =  it->second.looptid;
            m_eventloop_pool.erase(it);
        }
    }
    if(isexist)
    {
        g_log.Log(lv_debug, "TerminateLoop :%s .", name.c_str());
        if(wrapper.eventloop)
        {
            wrapper.eventloop->TerminateLoop();
            pthread_join(wrapper.looptid, NULL);
        }
    }
}

} }


