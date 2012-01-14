#ifndef  CORE_NET_SOCKWAITERBASE_H
#define  CORE_NET_SOCKWAITERBASE_H

#include "TcpSock.h"
#include "lock.hpp"
#include <deque>

namespace core { namespace net {

typedef std::deque< TcpSockSmartPtr > TcpSockContainerT;

class SockWaiterBase
{
public:
    SockWaiterBase();
    virtual ~SockWaiterBase();
    // add or update the tcp and the event you cared.
    bool AddTcpSock(TcpSockSmartPtr sp_tcp);
    TcpSockSmartPtr GetTcpSockByDestHost(const std::string& ip, unsigned short int port);
    // remove the event you cared on that fd, if no event set, the fd will be removed.
    void RemoveTcpSock(TcpSockSmartPtr sp_tcp);
    // remove all cared fds.
    void ClearTcpSock();
    // 主动关闭时只关闭写端,等待对方close的FIN包返回后本端再close
    void DisAllowAllTcpSend();
    bool Empty();
    // wait for ready event you has set cared about, every time you call wait will clear old ready event. 
    virtual int  Wait(TcpSockContainerT& allready, struct timeval& tv) = 0;
    bool IsTcpExist(TcpSockSmartPtr sp_tcp);
    int GetActiveTcpNum();
protected:
    void ClearClosedTcpSock();
    TcpSockContainerT  m_waiting_tcpsocks;
    core::common::locker m_waiting_tcpsocks_lock;

};
} }

#endif // end of CORE_NET_SOCKWAITERBASE_H
