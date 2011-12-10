#ifndef  CORE_NET_TCPSOCK_H
#define  CORE_NET_TCPSOCK_H

#include "SockHandler.h"
#include "SockEvent.hpp"
#include "lock.hpp"
#include <vector>
#include <boost/shared_array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace core { namespace net {

class TcpSock : private boost::noncopyable, public boost::enable_shared_from_this<TcpSock>
{
public:
    TcpSock();
    TcpSock(int fd, const std::string& ip, unsigned short int port);
    ~TcpSock();

    typedef std::vector< char > SockBufferT;

    int GetFD() const;
    bool  IsClosed() const;
    bool  SetNonBlock();
    void  SetCloseAfterExec();
    void  AddEvent(EventResult er);
    void  ClearEvent();
    bool  IsNeedWrite();
    void  DisAllowSend();
    bool  Writeable() const;
        // return false if buffer is full.
    // 将数据放到缓存,等待可以发送的时候自动发送
    bool SendData(const char* pdata, size_t size);
    void SetSockHandler(const SockHandler& cb);
    void HandleEvent();
    //const SockBufferT& GetInbuf() const;
    //const SockBufferT& GetOutbuf() const;
    bool GetDestHost(std::string& ip, unsigned short int& port) const;
    bool Connect(const std::string ip, unsigned short int port, struct timeval& tv_timeout); 
    int GetLastError();
    // set -1 to disable timeout.
    void SetTimeout(int to_ms);
    void UpdateTimeout();
    void RenewTimeout();
    // 服务端需要有主动关闭,因此改成public
    void  Close();
private:
    void  ShutDownWrite();
    // 每个fd都有2个缓冲区,一个输入,一个输出, 必须使用连续内存, 因此deque不能使用(deque分块连续)
    SockBufferT m_inbuf;
    SockBufferT m_outbuf;
    SockHandler m_sockcb;
    
    int  m_fd;
    // this flag indicate whether the tcp fd is really shutdown write.
    bool m_writeable;
    // this flag indicate whether more data is allowed to be send to the outbufffer.
    volatile bool m_allow_more_send;
    bool m_isclosed;
    // the event happened on the TcpSock
    SockEvent  m_sockev;
    int  m_alive_counter;
    int  m_errno;

    boost::shared_array<char> tmpbuf;
    core::common::locker m_tmpoutbuf_lock;
    SockBufferT  m_tmpoutbuf;
    struct DestHost
    {
        std::string host_ip;
        unsigned short int host_port;
    };
    DestHost  m_desthost;
    int  m_timeout_ms;
    int  m_timeout_renew;
    bool m_is_timeout_need;
};

typedef boost::shared_ptr< TcpSock > TcpSockSmartPtr;

struct IsSameTcpSock
{
    IsSameTcpSock(TcpSockSmartPtr sp_tcp)
        :left(sp_tcp)
    {
    }
    bool operator()(TcpSockSmartPtr right)
    {
        return left->GetFD() == right->GetFD();
    }
private:
    TcpSockSmartPtr left;
};

} }

#endif // end of CORE_NET_TCPSOCK_H
