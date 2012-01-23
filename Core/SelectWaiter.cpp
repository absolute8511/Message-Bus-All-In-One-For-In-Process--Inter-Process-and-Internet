#include "SelectWaiter.h"
#include "SockEvent.hpp"
#include "SimpleLogger.h"
#include "CommonUtility.hpp"

namespace core { namespace net { 

SelectWaiter::SelectWaiter()
{
}

SelectWaiter::~SelectWaiter()
{
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

    ClearClosedTcpSock();

    // the m_waiting_tcpsocks will be modified by other thread, so we copy it to a temp map.
    TcpSockContainerT tmp_tcpsocks;
    {
        core::common::locker_guard guard(m_waiting_tcpsocks_lock);
        tmp_tcpsocks = m_waiting_tcpsocks;
    }
    TcpSockContainerT::const_iterator cit = tmp_tcpsocks.begin();
    int maxfd = 0;
    while(cit != tmp_tcpsocks.end())
    {
        assert(*cit);
        int fd = (*cit)->GetFD();
        assert(fd != -1);
        if( fd == -1 )
        {
            ++cit;
            continue;
        }
        //printf("fd:%d added to readfds\n", fd);
        FD_SET(fd, &readfds);
        FD_SET(fd, &exceptfds);
        if( (*cit)->IsNeedWrite() )
        {
            FD_SET(fd, &writefds);
        }
        if(fd > maxfd)
            maxfd = fd;
        ++cit;
    }
    // got new tcp event as soon as Possible, we add the notify pipe to wait fd sets.
    FD_SET(m_notify_pipe[0], &readfds);
    if(m_notify_pipe[0] > maxfd)
        maxfd = m_notify_pipe[0];
    if(maxfd == 0)
    {
        //printf("tick:select start:%lld\n", (int64_t)utility::GetTickCount());
        // if tmp_tcpsocks is Empty, timeout need longer .
        ::select(0, NULL, NULL, NULL, &tv);
    }
    int retcode = ::select(maxfd + 1, &readfds, &writefds, &exceptfds, &tv);
    //printf("tick:select end:%lld, maxfd:%d, retcode:%d\n", (int64_t)utility::GetTickCount(),
    //    maxfd, retcode);

    GetAndClearNotify();
    
    TcpSockContainerT::iterator it = tmp_tcpsocks.begin();
    while(it != tmp_tcpsocks.end() && *it)
    {
        assert(*it);
        int fd = (*it)->GetFD();
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

