#include "SockWaiterBase.h"
#include "SimpleLogger.h"
#include "CommonUtility.hpp"
#include "EventLoop.h"
#include <algorithm>
#include <boost/bind.hpp>
#include <signal.h>
#include <fcntl.h>

namespace core { namespace net {

static LoggerCategory g_log("SockWaiterBase");

SockWaiterBase::SockWaiterBase()
    :m_newnotify(false),
    m_allactive(0)
{
	if (pipe(m_notify_pipe) < 0) {
		perror("pipe(notify_pipe) failed.");
	} else if ((fcntl(m_notify_pipe[0], F_SETFD, FD_CLOEXEC) == -1) ||
	    (fcntl(m_notify_pipe[1], F_SETFD, FD_CLOEXEC) == -1)) {
		perror("fcntl(notify_pipe, F_SETFD) failed."); 
		close(m_notify_pipe[0]);
		close(m_notify_pipe[1]);
	} else {
        utility::set_fd_nonblock(m_notify_pipe[0]);
        utility::set_fd_nonblock(m_notify_pipe[1]);
        m_running = true;
		return;
	}
	m_notify_pipe[0] = -1;	/* read end */
	m_notify_pipe[1] = -1;	/* write end */
}

SockWaiterBase::~SockWaiterBase()
{
    if(m_notify_pipe[0] != -1)
        close(m_notify_pipe[0]);
    if(m_notify_pipe[1] != -1)
        close(m_notify_pipe[1]);
}

void SockWaiterBase::DestroyWaiter()
{
    if(!m_running)
        return;
    assert(m_evloop->IsInLoopThread());
    core::common::locker_guard guard(m_common_lock);
    m_running = false;
    m_evloop = NULL;
    m_waiting_tcpsocks.clear();
}

void SockWaiterBase::SetEventLoop(EventLoop* pev)
{
    m_evloop = pev;
}

void SockWaiterBase::NotifyNewActive(kSockActiveNotify active)
{
    core::common::locker_guard guard(m_common_lock);
    NotifyNewActiveWithoutLock(active);
}

// should locker by caller.
void SockWaiterBase::NotifyNewActiveWithoutLock(kSockActiveNotify active)
{
	if (m_notify_pipe[1] != -1)
    {
        // only write to pipe for first notify.
        if(!m_newnotify)
        {
            int writed = write(m_notify_pipe[1], "1", 1);
            //g_log.Log(lv_debug, "write notify new active :%d", (int)active);
            if(writed != 1)
                g_log.Log(lv_error, "notify new active :%d failed !!!", active);
        }

        m_allactive |= active;

        m_newnotify = true;
    }
}

int SockWaiterBase::GetAndClearNotify()
{
    int allactive = NOACTIVE;
    bool needread = false;
    {
        core::common::locker_guard guard(m_common_lock);
        needread = m_newnotify;
        m_newnotify = false;
        allactive = m_allactive;
        m_allactive = 0;
    }
    while (needread)
    {
        char c;
        if (read(m_notify_pipe[0], &c, 1) == 1)
        {
            //g_log.Log(lv_debug, "got notify new active :%d, now num:%zu", c, m_waiting_tcpsocks.size());
            break;
        }
        else
        {
            if(errno != EAGAIN && errno != EINTR)
            {
                g_log.Log(lv_error, "get notify in sock waiter error.");
                return allactive;
            }
            if(errno == EAGAIN)
                break;
        }
    }
    return allactive;
}

bool SockWaiterBase::Empty() const
{
    return m_waiting_tcpsocks.empty();
}

//bool SockWaiterBase::IsTcpExist(TcpSockSmartPtr sp_tcp)
//{
//    if(!sp_tcp)
//        return false;
//    core::common::locker_guard guard(m_waiting_tcpsocks_lock);
//    TcpSockContainerT::iterator it = std::find_if(m_waiting_tcpsocks.begin(), m_waiting_tcpsocks.end(), IsSameTcpSock( sp_tcp ));
//    return (it != m_waiting_tcpsocks.end());
//}

int SockWaiterBase::GetActiveTcpNum() const
{
    return m_waiting_tcpsocks.size();
}

bool SockWaiterBase::UpdateTcpSock(TcpSockSmartPtr sp_tcp)
{
    if(!sp_tcp)
        return false;
    if(!m_running)
        return false;
    if(m_evloop->IsInLoopThread())
    {
        UpdateTcpSockInLoop(sp_tcp);
    }
    else
    {
        m_evloop->QueueTaskToLoop(boost::bind(&SockWaiterBase::UpdateTcpSock, this, sp_tcp));
    }
    return true;
}

void SockWaiterBase::UpdateTcpSockInLoop(TcpSockSmartPtr sp_tcp)
{
    if(!sp_tcp)
        return;

    if(!m_running)
        return;
    assert(m_evloop->IsInLoopThread());

    SockEvent ev = sp_tcp->GetCaredSockEvent();
    // add event to the real waiter(select's fds or epoll's fds)
    // changed: this will be called in the waiter thread.
    UpdateTcpSockEvent(sp_tcp);

    if(ev.hasAny())
    {
        if(!m_running)
            return;
        {
            //TcpSockContainerT::iterator waitingit = std::find_if(m_waiting_tcpsocks.begin(),
            //    m_waiting_tcpsocks.end(), IsSameTcpSock( sp_tcp ));
            //TcpSockContainerT::const_iterator waitingit = m_waiting_tcpsocks.find((long)sp_tcp.get());
            TcpSockContainerT::const_iterator waitingit = m_waiting_tcpsocks.find((long)sp_tcp->GetFD());
            if( waitingit == m_waiting_tcpsocks.end() )
            {
                // m_waiting_tcpsocks can only be modified by waiter thread.
                // so lock can put here to protect modified.
                //core::common::locker_guard guard(m_common_lock);
                //m_waiting_tcpsocks.push_back(sp_tcp);
                m_waiting_tcpsocks[(long)sp_tcp->GetFD()] = sp_tcp;
            }
            else
            {
                assert(sp_tcp->GetFD() == waitingit->second->GetFD());
                m_waiting_tcpsocks[(long)sp_tcp->GetFD()] = sp_tcp;
            }
        }
    }
    else
    {
        RemoveTcpSock(sp_tcp);
    }
}

bool SockWaiterBase::AddTcpSock(TcpSockSmartPtr sp_tcp)
{
    assert(m_evloop);
    SockEvent soev = sp_tcp->GetCaredSockEvent();
    soev.AddEvent(EV_READ);
    soev.AddEvent(EV_EXCEPTION);
    sp_tcp->SetCaredSockEvent(soev);
    sp_tcp->SetEventLoop(m_evloop);

    return UpdateTcpSock(sp_tcp);
}

// must be called in the waiter thread.
void SockWaiterBase::RemoveTcpSock(TcpSockSmartPtr sp_tcp)
{
    if(!m_running)
        return;
    assert(m_evloop->IsInLoopThread());
    sp_tcp->SetCaredSockEvent(SockEvent());
    // need remove.
    UpdateTcpSockEvent(sp_tcp);
    NotifyNewActive(REMOVED);
}

void SockWaiterBase::DisAllowAllTcpSend()
{
    if(!m_running)
        return;
    assert(m_evloop->IsInLoopThread());
    TcpSockContainerT::iterator it = m_waiting_tcpsocks.begin();
    while( it != m_waiting_tcpsocks.end() )
    {
        assert((*it).second);
        if((*it).second)
            (*it).second->DisAllowSend();
        ++it;
    }
}

static bool IsTcpClosed(TcpSockSmartPtr sp_tcp)
{
    if(sp_tcp)
        return sp_tcp->IsClosed();
    return true;
}

// after the tcp was closed, the waiter thread will
// clear all closed tcp in the waiting container. no 
// other thread expect the waiter thread can call this function. 
void SockWaiterBase::ClearClosedTcpSock()
{
    if(!m_running)
        return;
    //g_log.Log(lv_debug, "clear closed tcp");
    assert(m_evloop->IsInLoopThread());
    //core::common::locker_guard guard(m_common_lock);
    TcpSockContainerT::iterator reit = m_waiting_tcpsocks.begin();
    while(reit != m_waiting_tcpsocks.end())
    {
        if( IsTcpClosed(reit->second) )
        {
            m_waiting_tcpsocks.erase(reit++);
        }
        else
        {
            ++reit;
        }
    }
}

// clear all tcps when terminate.
// only called in waiter thread.
//void SockWaiterBase::ClearTcpSock()
//{
//    core::common::locker_guard guard(m_common_lock);
//    TcpSockContainerT::iterator begin = m_waiting_tcpsocks.begin();
//    while(begin != m_waiting_tcpsocks.end())
//    {
//        if(*begin)
//        {
//            UpdateTcpSockEvent(*begin, SockEvent());
//            (*begin)->SetCaredSockEvent(SockEvent());
//            (*begin)->SetSockWaiter(NULL);
//        }
//        ++begin;
//    }
//    m_waiting_tcpsocks.clear();
//}

} }

