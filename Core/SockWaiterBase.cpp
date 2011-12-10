#include "SockWaiterBase.h"
#include "SimpleLogger.h"
#include <algorithm>
#include <boost/bind.hpp>

namespace core { namespace net {

SockWaiterBase::SockWaiterBase()
{
}

SockWaiterBase::~SockWaiterBase()
{
    ClearTcpSock();    
}
bool SockWaiterBase::Empty()
{
    core::common::locker_guard guard(m_waiting_tcpsocks_lock);
    return m_waiting_tcpsocks.empty();
}

bool SockWaiterBase::AddTcpSock(TcpSockSmartPtr sp_tcp)
{
    if(!sp_tcp)
        return false;
    core::common::locker_guard guard(m_waiting_tcpsocks_lock);
    TcpSockContainerT::iterator it = std::find_if(m_waiting_tcpsocks.begin(), m_waiting_tcpsocks.end(), IsSameTcpSock( sp_tcp ));
    if(it == m_waiting_tcpsocks.end())
    {
        m_waiting_tcpsocks.push_back(sp_tcp);
    }
    return true;
}

void SockWaiterBase::RemoveTcpSock(TcpSockSmartPtr sp_tcp)
{
    if(!sp_tcp)
        return;
    core::common::locker_guard guard(m_waiting_tcpsocks_lock);
    TcpSockContainerT::iterator it = std::find_if(m_waiting_tcpsocks.begin(), m_waiting_tcpsocks.end(), IsSameTcpSock(sp_tcp));
    if(it != m_waiting_tcpsocks.end())
    {
        //(*it)->Close();
        m_waiting_tcpsocks.erase(it);
    }
}

TcpSockSmartPtr SockWaiterBase::GetTcpSockByDestHost(const std::string& ip, unsigned short int port)
{
    core::common::locker_guard guard(m_waiting_tcpsocks_lock);
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
    core::common::locker_guard guard(m_waiting_tcpsocks_lock);
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
void SockWaiterBase::ClearClosedTcpSock()
{
    core::common::locker_guard guard(m_waiting_tcpsocks_lock);
    TcpSockContainerT::iterator result = std::remove_if(m_waiting_tcpsocks.begin(), m_waiting_tcpsocks.end(), IsTcpClosed);
    m_waiting_tcpsocks.erase(result, m_waiting_tcpsocks.end());
}

void SockWaiterBase::ClearTcpSock()
{
    core::common::locker_guard guard(m_waiting_tcpsocks_lock);
    m_waiting_tcpsocks.clear();
}

} }

