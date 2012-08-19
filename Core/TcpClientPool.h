#ifndef  CORE_NET_CLIENT_POOL_H
#define  CORE_NET_CLIENT_POOL_H

#include "TcpSock.h"
#include "lock.hpp"
#include "SockWaiterBase.h"
#include <map>
#include <vector>
#include <boost/noncopyable.hpp>

namespace core { namespace net {

class TcpClientPool : public boost::noncopyable
{
public:
    typedef boost::function<bool(TcpSockSmartPtr)> PostCB;
    TcpSockSmartPtr GetTcpSockByDestHost(const std::string& ip, unsigned short int port);

    bool AddTcpSock(TcpSockSmartPtr sp_tcp);
    void RemoveTcpSock(TcpSockSmartPtr sp_tcp);
    bool CreateTcpSock(SockWaiterBase& waiter, const std::string& ip, unsigned short int port,
        int num, int timeout, SockHandler tcp_callback, PostCB postcb);
    TcpClientPool(){}
    ~TcpClientPool(){}

private:

    typedef std::vector< TcpSockSmartPtr > TcpSockContainerT;
    typedef std::map< std::pair<std::string, unsigned short int>, TcpSockContainerT > TcpSockPoolT;  
    typedef std::map< std::pair<std::string, unsigned short int>, int > TcpSockSelectCounterT;  

    // store all the tcp connection for the specified destip:destport.
    TcpSockPoolT       m_tcpclient_pool;
    core::common::locker m_common_lock;
    TcpSockSelectCounterT m_select_counter;

};
} }

#endif
