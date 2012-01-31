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
    if(m_prealloc_events == NULL)
    {
        g_log.Log(lv_error, "calloc returned null.");
    }
    struct epoll_event ev;
    if(m_notify_pipe[0] >= 0)
    {
        ev.events = EPOLLIN | EPOLLERR | EPOLLPRI | EPOLLET;
        ev.data.fd = m_notify_pipe[0];
        epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_notify_pipe[0], &ev);
    }
}

EpollWaiter::~EpollWaiter()
{
}

void EpollWaiter::DestroyWaiter()
{
    if(m_notify_pipe[0] != -1 && m_epfd >=0 )
    {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLERR | EPOLLPRI | EPOLLET;
        ev.data.fd = m_notify_pipe[0];
        epoll_ctl(m_epfd, EPOLL_CTL_DEL, m_notify_pipe[0], &ev);
    }
    if(m_epfd >= 0)
        close(m_epfd);
    if(m_prealloc_events)
    {
        free(m_prealloc_events);
        m_prealloc_events = NULL;
    }
    SockWaiterBase::DestroyWaiter();
}
// note: can not be locked by caller.
// only can be called in waiter thread.
bool EpollWaiter::UpdateTcpSockEvent(TcpSockSmartPtr sp_tcp)
{
    assert(sp_tcp);
    int fd = sp_tcp->GetFD();
    assert(fd != -1);
    if( fd == -1 )
    {
        return false;
    }
    SockEvent so_ev = sp_tcp->GetCaredSockEvent();
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLERR | EPOLLPRI | EPOLLET | EPOLLOUT;
    ev.data.fd = fd;
    epoll_ctl(m_epfd, EPOLL_CTL_DEL, fd, &ev);

    ev.events = 0;

    if(so_ev.hasRead())
    {
        ev.events |= EPOLLIN | EPOLLERR | EPOLLPRI | EPOLLET;
    }
    if(so_ev.hasWrite())
    {
        ev.events |= EPOLLOUT | EPOLLET;
    }
    if(so_ev.hasException())
    {
        ev.events |= EPOLLERR | EPOLLET;
    }
    if(so_ev.hasAny())
    {
        if(::epoll_ctl(m_epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
        {
            g_log.Log(lv_error, "add epoll fd failed,fd:%d", fd);
        }
    }
    return true;
}


int EpollWaiter::Wait(TcpSockContainerT& allready, struct timeval& tv)
{
    if(m_epfd < 0)
        return -1;
    allready.clear();

    struct epoll_event *events = m_prealloc_events;
    memset(events, 0, EPOLL_QUEUE_LEN*sizeof(struct epoll_event));
    TcpSockContainerT::const_iterator cit = m_waiting_tcpsocks.begin();
    std::map<int, TcpSockSmartPtr> tcpsocks_tmpmap;
    //bool writetimeout_detect = false; 
    while(cit != m_waiting_tcpsocks.end())
    {
        tcpsocks_tmpmap[(*cit)->GetFD()] = *cit;
        ++cit;
    }
    // the document said: the fd will be removed automatically when the fd is closed.
    int ms = tv.tv_usec/1000 + tv.tv_sec*1000;
    int retfds = ::epoll_wait(m_epfd, events, EPOLL_QUEUE_LEN, ms);
    // clear immediately after select waking up to speed up eventloop.
    int notifytype = GetAndClearNotify();
    if(notifytype & REMOVED)
    {
        ClearClosedTcpSock();
    }
    if(retfds < 0)
    {
        g_log.Log(lv_error, "error happened while epoll wait");
        return retfds;
    }
    //if(retfds == 0 && writetimeout_detect)
    //{
    //    printf("warning: wait to send data timeout, network may broken, data is not sended.\n");
    //    g_log.Log(lv_warn, "send data wait timeout, net broken");
    //}
    TcpSockContainerT::iterator tcp_it = m_waiting_tcpsocks.begin();
    // first update timeout for all waiting tcp.
    while(tcp_it != m_waiting_tcpsocks.end())
    {
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

