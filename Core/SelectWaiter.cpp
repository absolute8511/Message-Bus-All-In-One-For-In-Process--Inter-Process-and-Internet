#include "SelectWaiter.h"
#include "SockEvent.hpp"
#include "SimpleLogger.h"

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
    int retcode = ::select(maxfd + 1, &readfds, &writefds, &exceptfds, &tv);
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

