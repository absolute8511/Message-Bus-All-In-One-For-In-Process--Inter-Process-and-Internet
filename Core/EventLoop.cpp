#include "EventLoop.h"
#include "SockWaiterBase.h"
#include "SimpleLogger.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <boost/bind.hpp>

#define TIMEOUT_SHORT 2

using namespace boost;
namespace core { namespace net { 

static LoggerCategory g_log("EventLoop");

EventLoop::EventLoop()
{
    m_event_waiter.reset();
    m_terminal = false;
    m_islooprunning = false;
}

EventLoop::~EventLoop()
{
    m_terminal = true;
    //CloseAllClient();
    m_islooprunning = false;
}

// 主动关闭时只关闭写端,等待对方close的FIN包返回后本端再close
void EventLoop::CloseAllClient()
{
    assert(IsInLoopThread());
    if(m_event_waiter)
        m_event_waiter->DisAllowAllTcpSend();
}

bool EventLoop::IsInLoopThread()
{
    return pthread_equal(m_cur_looptid, pthread_self()) != 0;
}

bool EventLoop::AddTcpSockToLoop(TcpSockSmartPtr sp_tcp)
{
    if(m_terminal || m_event_waiter==NULL)
        return false;
    sp_tcp->SetEventLoop(this);
    if(!IsInLoopThread())
    {
        QueueTaskToLoop(boost::bind(&EventLoop::AddTcpSockToLoopInLoopThread,
                shared_from_this(), sp_tcp));
        return true;
    }
    AddTcpSockToLoopInLoopThread(sp_tcp);
    return true;
}

void EventLoop::AddTcpSockToLoopInLoopThread(TcpSockSmartPtr sp_tcp)
{
    assert(IsInLoopThread());
    m_event_waiter->AddTcpSock(sp_tcp);
}

bool EventLoop::UpdateTcpSock(TcpSockSmartPtr sp_tcp)
{
    assert(IsInLoopThread());
    if(m_event_waiter==NULL)
        return false;
    return m_event_waiter->UpdateTcpSock(sp_tcp);
}

void EventLoop::RemoveTcpSock(TcpSockSmartPtr sp_tcp)
{
    assert(IsInLoopThread());
    if(m_event_waiter==NULL)
        return;
    m_event_waiter->RemoveTcpSock(sp_tcp);
}

bool EventLoop::QueueTaskToLoop(EvTask task)
{
    {
        common::locker_guard guard(m_lock);
        m_pendings.push_back(task);
    }
    if(m_event_waiter)
        m_event_waiter->NotifyNewActive(UPDATEEVENT);
    return true;
}

void EventLoop::TerminateLoop()
{
    m_terminal = true;
    if(m_event_waiter)
        m_event_waiter->NotifyNewActive(TERMINATE);
}

int EventLoop::GetActiveTcpNum()
{
    if(m_terminal)
        return 0;
    if(m_event_waiter)
        return m_event_waiter->GetActiveTcpNum();
    return 0;
}

void EventLoop::SetSockWaiter(boost::shared_ptr<SockWaiterBase> spwaiter)
{
    if(spwaiter)
    {
        spwaiter->SetEventLoop(this);
        m_event_waiter = spwaiter;
    }
}

bool EventLoop::StartLoop(pthread_t& tid)
{
    if( m_islooprunning )
        return true;
    m_terminal = false;

    assert(m_event_waiter);

    shared_ptr<EventLoop>* selfRef = new shared_ptr<EventLoop>();
    *selfRef = shared_from_this();
 
    if(0 == pthread_create(&tid, NULL, &EventLoop::Loop, (void*)selfRef))
    {
        //printf("event loop :%lld started.\n", (uint64_t)tid);
        g_log.Log(lv_debug, "event loop : %lld started.", (uint64_t)tid);
        m_cur_looptid = tid;
        return true;
    }
    m_terminal = true;
    g_log.Log(lv_error, "start event loop thread failed.");
    return false;
}
void* EventLoop::Loop(void* param)
{

    shared_ptr<EventLoop> el = *(shared_ptr<EventLoop>*)param;
    delete reinterpret_cast<shared_ptr<EventLoop>*>(param);

    if(el == NULL || el->m_event_waiter == NULL)
    {
        g_log.Log(lv_debug, "null el or waiter in loop thread");
        return 0;
    }
    TcpSockContainerT readytcps;
    el->m_islooprunning = true;
    while(true)
    {
        struct timeval tv;
        tv.tv_sec = 10; //TIMEOUT_SHORT;
        tv.tv_usec = 200000;

        if(el->m_terminal)
        {// 关闭本地还活动的连接的写端,然后等待对方的响应后再彻底关闭连接,当所有的活动连接数都关闭后,再退出该事件循环体
            if(el->m_event_waiter->Empty())
                break;
            el->CloseAllClient();
        }
        // 这里的超时应该尽量短，因为第一个过来的新客户端的响应需要等到下次超时才能处理
        int retcode = el->m_event_waiter->Wait(readytcps, tv);

        if(retcode == -1)
        {
            g_log.Log(lv_error, "event loop select error.");
            if(el->m_terminal)
                break;
            continue;
        }

        std::vector<EventLoop::EvTask> tmptasks;
        {
            common::locker_guard guard(el->m_lock);
            tmptasks = el->m_pendings;
            el->m_pendings.clear();
        }

        for(size_t i = 0; i < tmptasks.size(); i++)
        {
            tmptasks[i]();
        }
        tmptasks.clear();


        if(el->m_terminal)
        {
            if(el->m_event_waiter->Empty())
                break;
            el->CloseAllClient();
        }
        if(retcode == 0)
        {
            if(el->m_terminal)
            {
                g_log.Log(lv_debug, "timeout while wait to terminate the event loop. ");
                break;
            }
            continue;
        }

        TcpSockContainerT::iterator ready_tcp_it = readytcps.begin();
        for( ; ready_tcp_it != readytcps.end(); ++ready_tcp_it) 
        {
            TcpSockSmartPtr sp_tcp = *ready_tcp_it;
            if(sp_tcp)
            {
                sp_tcp->HandleEvent();
            }
        }// end of while of readytcps process.

    }// end of while(true)
    el->m_event_waiter->DestroyWaiter();
    g_log.Log(lv_debug, "event loop:%ld exit loop.", (long)el->m_cur_looptid);
    el->m_islooprunning = false;
    return 0;
}

} }
