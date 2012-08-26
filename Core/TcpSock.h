#ifndef  CORE_NET_TCPSOCK_H
#define  CORE_NET_TCPSOCK_H

#include "SockHandler.h"
#include "SockEvent.hpp"
#include "lock.hpp"
#include "FastBuffer.h"
#include <vector>
#include <boost/shared_array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace core { namespace net {
class EventLoop;
class TcpSock : private boost::noncopyable, public boost::enable_shared_from_this<TcpSock>
{
public:
    TcpSock();
    TcpSock(int fd, const std::string& ip, unsigned short int port);
    ~TcpSock();

    //typedef std::vector< char > SockBufferT;
    typedef FastBuffer SockBufferT;

    int GetFD() const;
    bool  IsClosed() const;
    bool  SetNonBlock();
    void  SetCloseAfterExec();
    void  SetCaredSockEvent(SockEvent caredev);
    SockEvent GetCaredSockEvent() const;
    SockEvent GetCurrentEvent() const;
    void  AddEvent(EventResult er);
    void  ClearEvent();
    //bool  IsNeedWrite();
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
    int GetLastError() const;
    // set -1 to disable timeout.
    void SetTimeout(int to_ms);
    void UpdateTimeout();
    void RenewTimeout();
    void  SetEventLoop(EventLoop* pev);
    // 服务端需要有主动关闭时,调用DisAllowSend即可
    // if no response, the server can close the fd. must be called in loop thread
    void  Close(bool needremove = true);
private:
    void SendDataInLoop(const std::string& data);
    void SendDataInLoop(const char* pdata, size_t size);
    void  ShutDownWrite();
    bool DoSend();
    void AddAndUpdateEvent(EventResult er);
    void RemoveAndUpdateEvent(EventResult er);
    // 每个fd都有2个缓冲区,一个输入,一个输出, 必须使用连续内存, 因此deque不能使用(deque分块连续)
    SockBufferT m_inbuf;
    SockBufferT m_outbuf;
    SockHandler m_sockcb;
    
    int  m_fd;
    int  m_fd_w;  // dup fd for write only
    // this flag indicate whether the tcp fd is really shutdown write.
    bool m_writeable;
    // this flag indicate whether more data is allowed to be send to the outbufffer.
    volatile bool m_allow_more_send;
    bool m_isclosing;
    // the event happened on the TcpSock
    SockEvent  m_sockev;
    // the event I cared about.
    SockEvent  m_caredev;
    int  m_alive_counter;
    int  m_errno;

    //boost::shared_array<char> tmpbuf;
    //core::common::locker m_lock;
    //SockBufferT  m_tmpoutbuf;
    struct DestHost
    {
        std::string host_ip;
        unsigned short int host_port;
    };
    DestHost  m_desthost;
    int  m_timeout_ms;
    int  m_timeout_renew;
    bool m_is_timeout_need;
    EventLoop* m_evloop;
    int  m_tmp_blocksize;
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
        if(left->IsClosed() || right->IsClosed())
            return false;
        return left->GetFD() == right->GetFD();
    }
private:
    TcpSockSmartPtr left;
};

} }

#endif // end of CORE_NET_TCPSOCK_H
