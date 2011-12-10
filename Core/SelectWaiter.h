#ifndef  CORE_NET_SELECT_SOCKEVENTWAITER_H
#define  CORE_NET_SELECT_SOCKEVENTWAITER_H

#include "SockWaiterBase.h"

namespace core { namespace net {

class SelectWaiter : public SockWaiterBase
{
public:
    SelectWaiter();
    ~SelectWaiter();
    // must run in the event loop thread.
    int  Wait(TcpSockContainerT& allready, struct timeval& tv);
};
} }

#endif // end of CORE_NET_SELECT_SOCKEVENTWAITER_H
