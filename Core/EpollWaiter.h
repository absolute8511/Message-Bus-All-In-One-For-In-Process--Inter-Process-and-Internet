#ifndef  CORE_NET_EPOLL_SOCKEVENTWAITER_H
#define  CORE_NET_EPOLL_SOCKEVENTWAITER_H

#include "SockWaiterBase.h"
#include <sys/epoll.h>

namespace core { namespace net {

class EpollWaiter : public SockWaiterBase
{
public:
    EpollWaiter();
    ~EpollWaiter();
    // must run in the event loop thread.
    int  Wait(TcpSockContainerT& allready, struct timeval& tv);
private:
    int m_epfd;
    struct epoll_event*  m_prealloc_events;
};
} }

#endif // end of CORE_NET_EPOLL_SOCKEVENTWAITER_H
