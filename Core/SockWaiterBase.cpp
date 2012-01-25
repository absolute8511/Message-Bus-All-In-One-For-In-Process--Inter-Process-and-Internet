#include "SockWaiterBase.h"
#include "SimpleLogger.h"
#include "CommonUtility.hpp"
#include <algorithm>
#include <boost/bind.hpp>
#include <signal.h>
#include <fcntl.h>

namespace core { namespace net {

static LoggerCategory g_log("SockWaiterBase");

SockWaiterBase::SockWaiterBase()
    :m_newnotify(false)
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
		return;
	}
	m_notify_pipe[0] = -1;	/* read end */
	m_notify_pipe[1] = -1;	/* write end */
}

SockWaiterBase::~SockWaiterBase()
{
    //ClearTcpSock();    
    if(m_notify_pipe[0] != -1)
        close(m_notify_pipe[0]);
    if(m_notify_pipe[1] != -1)
        close(m_notify_pipe[1]);
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
		int writed = write(m_notify_pipe[1], &active, sizeof(active));
        m_newnotify = true;
        if(writed != sizeof(active))
            g_log.Log(lv_error, "notify new active :%d failed !!!", active);
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
    }
    while (needread)
    {
        kSockActiveNotify c = NOACTIVE;
        if (read(m_notify_pipe[0], &c, sizeof(c)) != -1)
        {
            //g_log.Log(lv_debug, "got notify new active :%d, now num:%zu", c, m_waiting_tcpsocks.size());
            allactive |= c;
            continue;
        }
        else
        {
            if(errno != EAGAIN && errno != EINTR)
            {
                g_log.Log(lv_error, "get notify in sock waiter error.");
                return NOACTIVE;
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

bool SockWaiterBase::UpdateTcpSock(TcpSockSmartPtr sp_tcp, SockEvent ev)
{
    if(!sp_tcp)
        return false;

    sp_tcp->SetCaredSockEvent(ev);
    // add event to the real waiter(select's fds or epoll's fds)
    UpdateTcpSockEvent(sp_tcp, ev);

    if(ev.hasAny())
    {
        sp_tcp->SetSockWaiter(this);

        bool isnew = false;
        {
            core::common::locker_guard guard(m_newadded_tcpsocks_lock);
            TcpSockContainerT::iterator it = std::find_if(m_newadded_tcpsocks.begin(),
                m_newadded_tcpsocks.end(), IsSameTcpSock( sp_tcp ));
            if(it == m_newadded_tcpsocks.end())
            {
                m_newadded_tcpsocks.push_back(sp_tcp);
                isnew = true;
            }
        }
        if(isnew)
        {
            NotifyNewActive(NEWADDED);
        }
        else
        {
            NotifyNewActive(UPDATEEVENT);
        }
    }
    else
    {
        // need remove.
        sp_tcp->SetSockWaiter(NULL);
        NotifyNewActive(REMOVED);
    }

    return true;
}

bool SockWaiterBase::AddTcpSock(TcpSockSmartPtr sp_tcp)
{
    SockEvent soev;
    soev.AddEvent(EV_READ);
    soev.AddEvent(EV_EXCEPTION);
    return UpdateTcpSock(sp_tcp, soev);
}

void SockWaiterBase::RemoveTcpSock(TcpSockSmartPtr sp_tcp)
{
    UpdateTcpSock(sp_tcp, SockEvent());
}

TcpSockSmartPtr SockWaiterBase::GetTcpSockByDestHost(const std::string& ip, unsigned short int port)
{
    core::common::locker_guard guard(m_common_lock);
    TcpSockContainerT::iterator it = m_waiting_tcpsocks.begin();
    while( it != m_waiting_tcpsocks.end() )
    {
        assert(*it);
        std::string destip;
        unsigned short int destport;
        if(*it)
        {
            (*it)->GetDestHost(destip, destport);
            if( destip == ip && destport == port )
            {
                if( !(*it)->IsClosed() && (*it)->Writeable() )
                    return *it;
                break;
            }
        }
        ++it;
    }
    return TcpSockSmartPtr();
}

void SockWaiterBase::DisAllowAllTcpSend()
{
    core::common::locker_guard guard(m_common_lock);
    TcpSockContainerT::iterator it = m_waiting_tcpsocks.begin();
    while( it != m_waiting_tcpsocks.end() )
    {
        assert(*it);
        if(*it)
            (*it)->DisAllowSend();
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
    core::common::locker_guard guard(m_common_lock);
    TcpSockContainerT::iterator result = std::remove_if(m_waiting_tcpsocks.begin(),
        m_waiting_tcpsocks.end(), IsTcpClosed);
    TcpSockContainerT::iterator erasestart = result;
    m_waiting_tcpsocks.erase(erasestart, m_waiting_tcpsocks.end());
}

// only called in waiter thread.
void SockWaiterBase::MergeNewAddedTcpSock()
{
    TcpSockContainerT tmp_newadded;
    {
        core::common::locker_guard guard(m_newadded_tcpsocks_lock);
        tmp_newadded = m_newadded_tcpsocks;
        m_newadded_tcpsocks.clear();
    }
    core::common::locker_guard guard(m_common_lock);
    TcpSockContainerT::iterator addedit = tmp_newadded.begin();
    while(addedit != tmp_newadded.end())
    {
        TcpSockContainerT::iterator it = std::find_if(m_waiting_tcpsocks.begin(),
            m_waiting_tcpsocks.end(), IsSameTcpSock( *addedit ));
        if(it == m_waiting_tcpsocks.end())
            m_waiting_tcpsocks.push_back(*addedit);
        ++addedit;
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

