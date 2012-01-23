#include "EpollWaiter.h"
#include "SockEvent.hpp"
#include "SimpleLogger.h"

#include <map>

#define EPOLL_QUEUE_LEN 32

namespace core { namespace net { 

static LoggerCategory g_log("EpollWaiter");

EpollWaiter::EpollWaiter()
    :m_epfd(-1)
{
    m_epfd = epoll_create(EPOLL_QUEUE_LEN);
    if(m_epfd < 0)
    {
        g_log.Log(lv_error, "epoll create fd failed.");
    }
    m_prealloc_events = (struct epoll_event*)calloc(EPOLL_QUEUE_LEN, sizeof(struct epoll_event));
}

EpollWaiter::~EpollWaiter()
{
    if(m_epfd >= 0)
        close(m_epfd);
    free(m_prealloc_events);
}

int EpollWaiter::Wait(TcpSockContainerT& allready, struct timeval& tv)
{
    if(m_epfd < 0)
        return -1;
    allready.clear();

    ClearClosedTcpSock();

    // the m_waiting_tcpsocks will be modified by other thread, so we copy it to a temp map.
    TcpSockContainerT tmp_tcpsocks;
    {
        core::common::locker_guard guard(m_waiting_tcpsocks_lock);
        tmp_tcpsocks = m_waiting_tcpsocks;
    }
    struct epoll_event ev;
    struct epoll_event *events = m_prealloc_events;
    memset(events, 0, EPOLL_QUEUE_LEN*sizeof(struct epoll_event));
    TcpSockContainerT::const_iterator cit = tmp_tcpsocks.begin();
    int maxfd = 0;
    std::map<int, TcpSockSmartPtr> tcpsocks_tmpmap;
    //bool writetimeout_detect = false; 
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
        ev.events = EPOLLIN | EPOLLERR | EPOLLPRI | EPOLLET;
        if( (*cit)->IsNeedWrite() )
        {
            ev.events |= EPOLLOUT;
            //writetimeout_detect = true;
        }
        ev.data.fd = fd;
        if(::epoll_ctl(m_epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
        {
            g_log.Log(lv_error, "add epoll fd failed,fd:%d", fd);
        }
        tcpsocks_tmpmap[fd] = *cit;
        if(fd > maxfd)
            maxfd = fd;
        ++cit;
    }
    if(m_notify_pipe[0] != 0)
    {
        ev.events = EPOLLIN | EPOLLERR | EPOLLPRI | EPOLLET;
        ev.data.fd = m_notify_pipe[0];
        epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_notify_pipe[0], &ev);
        if(m_notify_pipe[0] > maxfd)
            maxfd = m_notify_pipe[0];
    }
    // the document said: the fd will be removed automatically when the fd is closed.
    int ms = tv.tv_usec/1000 + tv.tv_sec*1000;
    int retfds = ::epoll_wait(m_epfd, events, EPOLL_QUEUE_LEN, ms);
    //if(retfds > 0)
    //    g_log.Log(lv_debug, "epoll wait return, retfds:%d", retfds);
    if(retfds < 0)
    {
        g_log.Log(lv_error, "error happened while epoll wait");
        return retfds;
    }
    GetAndClearNotify();
    //if(retfds == 0 && writetimeout_detect)
    //{
    //    printf("warning: wait to send data timeout, network may broken, data is not sended.\n");
    //    g_log.Log(lv_warn, "send data wait timeout, net broken");
    //}
    // clean the cared fd
    TcpSockContainerT::iterator tcp_it = tmp_tcpsocks.begin();
    while(tcp_it != tmp_tcpsocks.end())
    {
        ev.events = EPOLLIN | EPOLLERR | EPOLLPRI | EPOLLET | EPOLLOUT;
        ev.data.fd = (*tcp_it)->GetFD();
        epoll_ctl(m_epfd, EPOLL_CTL_DEL, (*tcp_it)->GetFD(), &ev);
        // first update timeout for all waiting tcp.
        (*tcp_it)->UpdateTimeout();
        ++tcp_it;
    }
    for(int i = 0; i < retfds; i++)
    {
        if(events[i].data.fd == m_notify_pipe[0])
            continue;
        TcpSockSmartPtr sptcp = tcpsocks_tmpmap[events[i].data.fd];
        assert(sptcp);
        sptcp->ClearEvent();
        bool isready = false;
        if( (events[i].events & EPOLLIN) ||
            (events[i].events & EPOLLPRI) )
        {
            sptcp->AddEvent(EV_READ);
            isready = true;
        }
        if(events[i].events & EPOLLOUT)
        {
            sptcp->AddEvent(EV_WRITE);
            isready = true;
        }
        if(events[i].events & EPOLLERR)
        {
            sptcp->AddEvent(EV_EXCEPTION);
            isready = true;
        }
        // in epoll, only ready fd will be returned, so at least one event 
        // will be set in events struct.
        assert(isready);
        if(isready)
        {
            sptcp->RenewTimeout();
            allready.push_back(sptcp);
            //g_log.Log(lv_debug, "fd:%d has been added to ready socket, total ready:%zu", sptcp->GetFD(), allready.size());
        }
    }
    return retfds;
}

} }

