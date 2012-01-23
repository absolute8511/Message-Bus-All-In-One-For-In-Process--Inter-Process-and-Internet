#include "EventLoopPool.h"
#include "SimpleLogger.h"
#include <pthread.h>
#include <boost/shared_ptr.hpp>
#include <vector>
#include <stdio.h>
#if defined (__APPLE__) || defined (__MACH__) 
#include "SelectWaiter.h"
#else
#include "EpollWaiter.h"
#endif

#define MAX_TCPNUM 10

namespace core { namespace net {

struct EventLoopWrapper
{
    boost::shared_ptr< EventLoop > eventloop;
    pthread_t looptid;
};
typedef std::map<std::string, EventLoopWrapper > EventLoopContainerT;


static EventLoopContainerT  m_eventloop_pool;
static EventLoopContainerT  m_innerloop_pool;
static core::common::locker m_pool_locker;

static LoggerCategory g_log("EventLoopPool");

//static void new_tcp_sighandler(int sig)
//{
//    g_log.Log(lv_debug, "new tcp sig handler:%d", sig);
//}

EventLoopPool::EventLoopPool()
{
}

bool EventLoopPool::InitEventLoopPool()
{
    return true;
}

void EventLoopPool::DestroyEventLoopPool()
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
    EventLoopContainerT::iterator inner_it = m_innerloop_pool.begin();
    while(inner_it != m_innerloop_pool.end())
    {
        inner_it->second.eventloop->TerminateLoop();
        jointids.push_back(inner_it->second.looptid);
        ++inner_it;
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

bool EventLoopPool::AddTcpSockToInnerLoop(TcpSockSmartPtr sp_tcp)
{
    core::common::locker_guard guard(m_pool_locker);
    EventLoopContainerT::iterator it = m_innerloop_pool.begin();
    boost::shared_ptr<EventLoop> addedev;
    while(it != m_innerloop_pool.end())
    {
        if(it->second.eventloop)
        {
            if(it->second.eventloop->GetActiveTcpNum() < MAX_TCPNUM)
            {
                addedev = it->second.eventloop;
                break;
            }
        }
        ++it;
    }
    if(addedev == NULL || addedev->GetActiveTcpNum() > MAX_TCPNUM)
    {
        boost::shared_ptr<EventLoop> sock_el(new EventLoop);
#if defined (__APPLE__) || defined (__MACH__) 
        boost::shared_ptr< SockWaiterBase > spwaiter(new SelectWaiter());
#else
        //boost::shared_ptr< SockWaiterBase > spwaiter(new SelectWaiter());
        boost::shared_ptr< SockWaiterBase > spwaiter(new EpollWaiter());
#endif
        sock_el->SetSockWaiter(spwaiter);
        pthread_t tid;
        if (sock_el->StartLoop(tid))
        {
            addedev = sock_el;
            std::string name = "innerloop_";
            std::string id;
            id.push_back('a' + m_innerloop_pool.size());
            EventLoopWrapper wrapper;
            wrapper.eventloop = sock_el;
            wrapper.looptid = tid;
            m_innerloop_pool[name + id] = wrapper;
            g_log.Log(lv_debug, "new inner tcp event loop:%s started, tid:%ld.", id.c_str(), (long)tid);
        }
        else
        {
            g_log.Log(lv_warn, "start inner event loop failed.");
            return false;
        }
    }
    if(addedev)
    {
        addedev->AddTcpSockToLoop(sp_tcp);
        g_log.Log(lv_debug, "inner tcp num:%d .", addedev->GetActiveTcpNum());
        return true;
    }
    return false;
}

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


