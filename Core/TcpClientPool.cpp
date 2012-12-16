#include "TcpClientPool.h"
#include "SimpleLogger.h"
#include "EventLoopPool.h"
#include <pthread.h>
#include <boost/shared_ptr.hpp>
#include <vector>
#include <stdio.h>

namespace core { namespace net {

static LoggerCategory g_log("TcpClientPool");

TcpSockSmartPtr TcpClientPool::GetTcpSockByDestHost(const std::string& ip, unsigned short int port)
{
    common::locker_guard g(m_common_lock);
    TcpSockPoolT::const_iterator it = m_tcpclient_pool.find(std::make_pair(ip, port));
    if(it != m_tcpclient_pool.end() && it->second.size() > 0)
    {
        const TcpSockContainerT& all_conn = it->second;
        assert(m_select_counter.find(std::make_pair(ip, port)) != m_select_counter.end());
        return all_conn[m_select_counter[std::make_pair(ip, port)]++ % all_conn.size()];
    }
    return TcpSockSmartPtr();
}

bool TcpClientPool::AddTcpSock(TcpSockSmartPtr sp_tcp)
{
    common::locker_guard g(m_common_lock);
    std::string ip;
    unsigned short int port;
    if(sp_tcp)
    {
        if(sp_tcp->GetDestHost(ip, port))
        {
            TcpSockPoolT::iterator it = m_tcpclient_pool.find(std::make_pair(ip, port));
            if(it != m_tcpclient_pool.end())
            {
                g_log.Log(lv_debug, "more same client connection added");
                it->second.push_back(sp_tcp);
            }
            else
            {
                g_log.Log(lv_debug, "first new client connection added");
                m_tcpclient_pool[std::make_pair(ip, port)].push_back(sp_tcp);
                m_select_counter[std::make_pair(ip, port)] = 0;
            }
            return true;
        }
        else
        {
            g_log.Log(lv_info, "failed to add a tcp sock without host info.");
        }
    }
    return false;
}

void TcpClientPool::RemoveTcpSock(TcpSockSmartPtr sp_tcp)
{
    common::locker_guard g(m_common_lock);
    std::string ip;
    unsigned short int port;
    if(sp_tcp)
    {
        if(sp_tcp->GetDestHost(ip, port))
        {
            TcpSockPoolT::iterator it = m_tcpclient_pool.find(std::make_pair(ip, port));
            if(it != m_tcpclient_pool.end())
            {
                TcpSockContainerT::iterator conn_it = it->second.begin();
                while(conn_it != it->second.end())
                {
                    if(IsSameTcpSock(sp_tcp)(*conn_it))
                    {
                        g_log.Log(lv_debug, "client connection removed from pool, %s:%u, fd:%d", ip.c_str(), port, sp_tcp->GetFD());
                        conn_it = it->second.erase(conn_it);
                        continue;
                    }
                    ++conn_it;
                }
            }
            else
            {
                g_log.Log(lv_debug, "tcp client connection not exist, fd:%d", sp_tcp->GetFD());
            }
        }
        else
        {
            g_log.Log(lv_info, "failed to remove a tcp sock without host info.");
        }
    }
}

bool TcpClientPool::CreateTcpSock(const std::string& loopname, const std::string& ip, unsigned short int port,
    int num, int timeout, SockHandler tcp_callback, PostCB postcb)
{
    int realneed_num = num;
    {
        common::locker_guard g(m_common_lock);
        TcpSockPoolT::const_iterator it = m_tcpclient_pool.find(std::make_pair(ip, port));
        if(it != m_tcpclient_pool.end())
        {
            const TcpSockContainerT& all_conn = it->second;
            assert(m_select_counter.find(std::make_pair(ip, port)) != m_select_counter.end());
            realneed_num = realneed_num - (int)all_conn.size();
        }
    }

    while(--realneed_num >= 0)
    {
        struct timeval tv;
        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        TcpSockSmartPtr newtcp;
        newtcp.reset(new TcpSock());
        // 连接指定客户端并发送数据 
        bool connected = newtcp->Connect(ip, port, tv);
        if ( !connected )
        {
            return false;
        }
        newtcp->SetNonBlock();
        newtcp->SetCloseAfterExec();
        newtcp->SetSockHandler(tcp_callback);

        if(!loopname.empty())
        {
            if(!EventLoopPool::AddTcpSockToLoop(loopname, newtcp))
                return false;
        }
        else
        {
            if(!EventLoopPool::AddTcpSockToInnerLoop(newtcp))
                return false;
        }
        if(!postcb(newtcp))
        {
            return false;
        }

        common::locker_guard g(m_common_lock);
        TcpSockPoolT::iterator it = m_tcpclient_pool.find(std::make_pair(ip, port));
        if(it != m_tcpclient_pool.end())
        {
            g_log.Log(lv_debug, "more same client connection added, %s:%d, fd:%d", ip.c_str(), port, newtcp->GetFD());
            it->second.push_back(newtcp);
        }
        else
        {
            g_log.Log(lv_debug, "first new client connection added, %s:%d, fd:%d", ip.c_str(), port, newtcp->GetFD());
            m_tcpclient_pool[std::make_pair(ip, port)].push_back(newtcp);
            m_select_counter[std::make_pair(ip, port)] = 0;
        }
    }
    return true;
}


} }
