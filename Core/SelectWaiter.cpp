#include "SelectWaiter.h"
#include "SockEvent.hpp"
#include "SimpleLogger.h"
#include "CommonUtility.hpp"

#include <errno.h>

namespace core { namespace net { 

SelectWaiter::SelectWaiter()
{
    maxfd = 0;
    FD_ZERO(&m_readfds);
    FD_ZERO(&m_writefds);
    FD_ZERO(&m_exceptfds);
    // got new tcp event as soon as Possible, we add the notify pipe to wait fd sets.
    FD_SET(m_notify_pipe[0], &m_readfds);
    if(m_notify_pipe[0] > maxfd)
        maxfd = m_notify_pipe[0];
}

SelectWaiter::~SelectWaiter()
{
}

void SelectWaiter::DestroyWaiter()
{
    if(m_notify_pipe[0] != -1)
        FD_CLR(m_notify_pipe[0], &m_readfds);
    SockWaiterBase::DestroyWaiter();
}

// note: can not be locked by caller.
// only can be called in waiter thread.
bool SelectWaiter::UpdateTcpSockEvent(TcpSockSmartPtr sp_tcp)
{
    assert(sp_tcp);
    int fd = sp_tcp->GetFD();
    assert(fd != -1);
    if( fd == -1 )
    {
        return false;
    }
    SockEvent so_ev = sp_tcp->GetCaredSockEvent();
    FD_CLR(fd, &m_readfds);
    FD_CLR(fd, &m_exceptfds);
    FD_CLR(fd, &m_writefds);

    if(so_ev.hasRead())
    {
        FD_SET(fd, &m_readfds);
    }
    if(so_ev.hasWrite())
    {
        FD_SET(fd, &m_writefds);
    }
    if(so_ev.hasException())
    {
        FD_SET(fd, &m_exceptfds);
    }
    if(fd > maxfd)
        maxfd = fd;
    return true;
}

int SelectWaiter::Wait(TcpSockContainerT& allready, struct timeval& tv)
{
    allready.clear();
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    
    readfds = m_readfds;
    writefds = m_writefds;
    exceptfds = m_exceptfds;
    if(maxfd == 0)
    {
        // if tmp_tcpsocks is Empty, timeout need longer .
        ::select(0, NULL, NULL, NULL, &tv);
    }
    int retcode = ::select(maxfd + 1, &readfds, &writefds, &exceptfds, &tv);

    // clear immediately after select waking up to speed up eventloop.
    int notifytype = GetAndClearNotify();
    if(notifytype & REMOVED)
    {
        ClearClosedTcpSock();
    }

    if(retcode == -1 && errno == EBADF)
    {
        // some closed fd did not clean properly.
        //
        assert(false);
        FD_ZERO(&m_readfds);
        FD_ZERO(&m_writefds);
        FD_ZERO(&m_exceptfds);
        TcpSockContainerT::iterator it = m_waiting_tcpsocks.begin();
        while(it != m_waiting_tcpsocks.end() && *it)
        {
            UpdateTcpSockEvent(*it);
            ++it;
        }
        FD_SET(m_notify_pipe[0], &m_readfds);
        if(m_notify_pipe[0] > maxfd)
            maxfd = m_notify_pipe[0];
        return retcode;
    }

    // the new added tcp container and waiting tcp container have been seperated,
    // so no lock need here. no other thread can modify the waiting tcp container.
    TcpSockContainerT::iterator it = m_waiting_tcpsocks.begin();
    while(it != m_waiting_tcpsocks.end() && *it)
    {
        assert(*it);
        int fd = (*it)->GetFD();
        assert(fd != -1);
        (*it)->ClearEvent();
        bool isready = false;
        if(FD_ISSET(fd, &readfds))
        {
            (*it)->AddEvent(EV_READ);
            isready = true;
        }
        if(FD_ISSET(fd, &writefds))
        {
            (*it)->AddEvent(EV_WRITE);
            isready = true;
        }
        if(FD_ISSET(fd, &exceptfds))
        {
            (*it)->AddEvent(EV_EXCEPTION);
            isready = true;
        }
        if(isready)
        {
            (*it)->RenewTimeout();
            allready.push_back(*it);
        }
        else
        {
            // whether timeout is ready.
            (*it)->UpdateTimeout();
        }

        ++it;
    }
    return retcode;
}

} }

