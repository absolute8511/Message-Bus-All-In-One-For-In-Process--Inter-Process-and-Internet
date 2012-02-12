#include "TcpSock.h"
#include "CommonUtility.hpp"
#include "SimpleLogger.h"
#include "EventLoop.h"
#include <fcntl.h>
#include <errno.h>
#include <map>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <boost/bind.hpp>

#define BLOCK_SIZE 1024*8
#define MAX_BUF_SIZE BLOCK_SIZE*64*8
#define ALIVE_NUM  5

namespace core { namespace net {

static LoggerCategory g_log("TcpSock");

TcpSock::TcpSock()
    :m_fd(-1),
    m_writeable(false),
    m_allow_more_send(false),
    m_isclosing(true),
    m_alive_counter(-1),
    m_timeout_ms(-1),
    m_is_timeout_need(false),
    m_evloop(NULL)
{
    tmpbuf.reset(new char[BLOCK_SIZE]);
    m_tmp_blocksize = BLOCK_SIZE;
}
TcpSock::TcpSock(int fd, const std::string& ip, unsigned short int port)
    :m_fd(fd),
    m_writeable(true),
    m_allow_more_send(true),
    m_isclosing(false),
    m_alive_counter(ALIVE_NUM),
    m_timeout_ms(-1),
    m_is_timeout_need(false),
    m_evloop(NULL)
{
    m_desthost.host_ip = ip;
    m_desthost.host_port = port;
    //g_log.Log(lv_debug, "new client tcp %s:%d.fd:%d", ip.c_str(), port, m_fd);
    tmpbuf.reset(new char[BLOCK_SIZE]);
    m_tmp_blocksize = BLOCK_SIZE;
}

TcpSock::~TcpSock()
{
    Close(false);
}

bool TcpSock::SetNonBlock()
{
    if(IsClosed())
        return false;
    long fdflag;
    if( (fdflag = fcntl(m_fd, F_GETFL, NULL)) < 0 )
    {
        m_errno = errno;
        g_log.Log(lv_error, "fcntl failed.");
        return false;
    }
    return fcntl(m_fd, F_SETFL, fdflag|O_NONBLOCK) >= 0;
}

void TcpSock::SetCloseAfterExec()
{   
    long fdflag;
    if( (fdflag = fcntl(m_fd, F_GETFD, NULL)) < 0 )
    {
        m_errno = errno;
        g_log.Log(lv_error, "fcntl failed.");
        return;
    }
    int ret = fcntl(m_fd, F_SETFD, fdflag|FD_CLOEXEC);
    assert(ret >= 0);
}

void TcpSock::SetCaredSockEvent(SockEvent caredev)
{
    m_caredev = caredev;
}

SockEvent TcpSock::GetCaredSockEvent() const
{
    return m_caredev;
}

int TcpSock::GetFD() const
{
    assert(m_fd>=0);
    return m_fd;
}

void TcpSock::RenewTimeout()
{
    //g_log.Log(lv_debug, "renew timeout : fd-%d", m_fd);
    if(m_is_timeout_need)
        m_timeout_ms = core::utility::GetTickCount() + m_timeout_renew;
}

void TcpSock::SetTimeout(int to_ms)
{
    if(to_ms <= 0)
    {
        m_is_timeout_need = false;
        return;
    }

    if(to_ms < 1000)
        g_log.Log(lv_debug, "so litter timeout, you should use a timer thread. timeout:%d", to_ms);
    m_timeout_renew = to_ms;
    m_timeout_ms = core::utility::GetTickCount() + to_ms;
    m_is_timeout_need = true;
}

void TcpSock::UpdateTimeout()
{
    if(m_is_timeout_need)
    {
        //g_log.Log(lv_debug, "update timeout : fd-%d", m_fd);
        if(core::utility::GetTickCount() > m_timeout_ms)
        {
            RenewTimeout();
            if(m_sockcb.onTimeout)
                m_sockcb.onTimeout(shared_from_this());
            else
                m_is_timeout_need = false;
        }
    }
}

int TcpSock::GetLastError() const
{
    return m_errno;
}

// the tcp sock has data to send out to network.
bool TcpSock::IsNeedWrite()
{
    assert(m_evloop->IsInLoopThread());
    if(IsClosed() || !Writeable())
        return false;
    return !m_outbuf.empty();
}

// the caller must protect the func use lock.
void TcpSock::ShutDownWrite()
{
    if(IsClosed())
        return;
    assert(m_evloop->IsInLoopThread());
    m_writeable = false;
    // before shutdown we try send the left data directly.
    if(!m_outbuf.empty() /*&& (write(m_fd, &m_outbuf[0], m_outbuf.size()) != (int)m_outbuf.size())*/)
    {
        g_log.Log(lv_warn, "outbuf is not empty while shutdown write, some data may not sended.");
    }
    ::shutdown(m_fd, SHUT_WR);
    m_outbuf.clear();
}
void TcpSock::DisAllowSend()
{
    // if no data to write , we can really to shutdown the write of fd.
    // or we should wait for the data be sended.
    m_allow_more_send = false;
    if(m_evloop)
    {
        if(!m_evloop->IsInLoopThread())
            m_evloop->QueueTaskToLoop(boost::bind(&TcpSock::DisAllowSend, shared_from_this()));
        else if(m_outbuf.empty())
        {
            ShutDownWrite();
        }
        else
        {
            m_caredev.AddEvent(EV_WRITE);
            m_evloop->UpdateTcpSock(shared_from_this());
        }
    }
}

void TcpSock::Close(bool needremove)
{
    if(m_fd == -1 || m_isclosing)
        return;
    assert(m_evloop->IsInLoopThread());
    m_isclosing = true;
    m_writeable = false;
    m_allow_more_send = false;
    if(m_evloop && needremove)
    {
        m_evloop->RemoveTcpSock(shared_from_this());
        m_evloop = NULL;
    }

    //m_outbuf.insert(m_outbuf.end(), m_tmpoutbuf.begin(), m_tmpoutbuf.end());
    if(!m_outbuf.empty() /*&& (write(m_fd, &m_outbuf[0], m_outbuf.size()) != (int)m_outbuf.size())*/)
    {
        g_log.Log(lv_warn, "outbuf is not empty while close socket realfd, some data may not sended.");
    }
    //m_tmpoutbuf.clear();
    if(m_fd != -1)
    {
        //g_log.Log(lv_debug, "the tcp is going to close. fd:%d.\n", m_fd);
        // mark as closed first, because any new tcp created by other thread
        // will immediately reuse the fd before the m_fd be setted to -1.
        ::close(m_fd);
        m_fd = -1;
    }
    m_inbuf.clear();
    m_outbuf.clear();
    m_alive_counter = -1;
    m_desthost.host_ip = "";
    m_is_timeout_need = false;
}

bool TcpSock::IsClosed() const
{
    if( m_fd == -1)
        return true;
    return m_isclosing;
}

bool TcpSock::Writeable() const
{
    return m_writeable && m_allow_more_send;
}

void TcpSock::SendDataInLoop(const std::string& data)
{
    SendDataInLoop(data.data(), data.size());
}

void TcpSock::SendDataInLoop(const char* pdata, size_t size)
{
    if(IsClosed() || !Writeable())
    {
        m_errno = EPIPE;
        return;
    }
    m_outbuf.push_back(pdata, size);
    if(m_evloop)
    {
        m_caredev.AddEvent(EV_WRITE);
        m_evloop->UpdateTcpSock(shared_from_this());
    }
}

bool TcpSock::SendData(const char* pdata, size_t size)
{
    if(size > 0 && pdata)
    {
        try
        {
            if(IsClosed() || !Writeable())
            {
                m_errno = EPIPE;
                return false;
            }
            if( (m_outbuf.size() + size) > MAX_BUF_SIZE )
            {
                m_errno = 0;
                g_log.Log(lv_warn, "buffer overflow , please slow down send.");
                return false;
            }
            if(!m_evloop->IsInLoopThread())
            {
                m_evloop->QueueTaskToLoop(boost::bind(&TcpSock::SendDataInLoop, shared_from_this(),
                        std::string(pdata, size)));
                return true;
            }
            SendDataInLoop(pdata, size);
            return true;
        }
        catch(...)
        {
        }
    }
    g_log.Log(lv_error, "send data to outbuf error.");
    return false;
}
void TcpSock::SetSockHandler(const SockHandler& cb)
{
    m_sockcb = cb;
}

void TcpSock::SetEventLoop(EventLoop* pev)
{
    if(m_evloop != pev)
        m_evloop = pev;
}

void TcpSock::ClearEvent()
{
    m_sockev.ClearEvent();
}

void TcpSock::AddEvent(EventResult er)
{
    m_sockev.AddEvent(er);
}
void TcpSock::HandleEvent()
{
    assert(m_evloop->IsInLoopThread());
    if(IsClosed())
        return;
    /*if( m_outbuf.size() < MAX_BUF_SIZE)
    {
        //core::common::locker_guard guard(m_lock);
        m_outbuf.insert(m_outbuf.end(), m_tmpoutbuf.begin(), m_tmpoutbuf.end());
        m_tmpoutbuf.clear();
    }*/
    if( m_sockev.hasRead() )
    {
        // edge triggered mode in epoll will not notify the old event again,
        // so we should keep reading or writing until EAGAIN happened.
        while(true)
        {
            // reuse the tmpbuf to store the readed data.
            int readed = read(m_fd, tmpbuf.get(), m_tmp_blocksize);
            if(readed == 0)
            {
                if(m_sockcb.onClose)
                {
                    m_sockcb.onClose(shared_from_this());
                }
                //g_log.Log(lv_debug, "fd:%d onread = 0, to close.", m_fd);
                Close();
                // fd is not available, so here should find the next event.
                return;
            }
            else if(readed > 0)
            {
                //m_inbuf.insert(m_inbuf.end(), tmpbuf.get(), tmpbuf.get() + readed);
                m_inbuf.push_back(tmpbuf.get(), readed);
                if(readed == m_tmp_blocksize)
                {
                    g_log.Log(lv_debug, "resizing the tmpbuf size to %d.", m_tmp_blocksize*2);
                    tmpbuf.reset(new char[m_tmp_blocksize*2]);
                    m_tmp_blocksize *= 2;
                }
                size_t n = 0;
                if(m_sockcb.onRead)
                {
                    n = m_sockcb.onRead(shared_from_this(), m_inbuf.data(), m_inbuf.size());
                }
                else
                {
                    n = m_inbuf.size();
                }
                if(n > 0)
                {
                    if(n > m_inbuf.size())
                    {
                        g_log.Log(lv_debug, "you should not close directly in onRead handler.");
                        m_inbuf.clear();
                    }
                    else
                    {
                        m_inbuf.pop_front(n);
                    }
                }
            }
            else
            {
                if((errno != EAGAIN) && (errno != EINTR))
                {
                    m_errno = errno;
                    if(m_sockcb.onError)
                    {
                        m_sockcb.onError(shared_from_this());
                    }
                    g_log.Log(lv_error, "read error on fd, error fd:%d.", m_fd);
                    Close();
                    return;
                }
                // errno is EAGAIN, end reading.
                if(errno == EAGAIN)
                    break;
            }
        }
    }
    if( m_sockev.hasWrite() && m_writeable )
    {
        while(true)
        {
            int writed = write(m_fd, m_outbuf.data(), m_outbuf.size());
            if(writed > 0)
            {
                //g_log.Log(lv_debug, "write on fd:%d. bytes:%d", m_fd, writed);
                m_outbuf.pop_front(writed);
                if(m_outbuf.empty())
                {
                    if(m_evloop)
                    {
                        m_caredev.RemoveEvent(EV_WRITE);
                        m_evloop->UpdateTcpSock(shared_from_this());
                    }
                    if(m_sockcb.onSend)
                    {
                        if(!m_sockcb.onSend(shared_from_this()))
                        {// return false to indicate disallow for more sending.
                            // we will try to send the data left in the buffer,
                            // but not allow more data to add to the buffer.
                            DisAllowSend();
                        }
                    }
                    if(!m_allow_more_send)
                    {
                        // if no data in buffer and disallow more data be added to buffer
                        // it means we can shutdown the tcp fd since no more data will be sended.
                        ShutDownWrite();
                        return; 
                        // fd is not available, so here should find the next event.
                    }
                }
            }
            else
            {
                if(writed == 0)
                    break;
                if((errno != EAGAIN) && (errno != EINTR))
                {
                    m_errno = errno;
                    if(m_sockcb.onError)
                    {
                        m_sockcb.onError(shared_from_this());
                    }
                    g_log.Log(lv_error, "write error on fd, error fd:%d.", m_fd);
                    Close();
                    return;
                }
                // errno is EAGAIN, end writing.
                if(errno == EAGAIN)
                    break;
            }
        }
    }
    if( m_sockev.hasException() )
    {
        m_errno = errno;
        if(m_sockcb.onError)
        {
            m_sockcb.onError(shared_from_this());
        }
        g_log.Log(lv_error, "exception happened, fd:%d.", m_fd);
        Close();
        return;
    }
}

bool TcpSock::Connect(const std::string ip, unsigned short int port, struct timeval& tv_timeout)
{
    //Close();
    assert(m_fd == -1);
    struct sockaddr_in dest_address;
    dest_address.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &dest_address.sin_addr);
    //dest_address.sin_addr.s_addr = inet_addr(ip.c_str());
    dest_address.sin_port = htons(port);

    //printf("ready to connect the dest client to send message. ip:port - %s:%d\n", ip.c_str(), port);
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(m_fd == -1)
    {
        m_errno = errno;
        g_log.Log(lv_error, "create socket fd failed while connect.", m_fd);
        return false;
    }
    SetCloseAfterExec();
    SetNonBlock();
    // 连接指定客户端并发送数据 
    if (0 != connect(m_fd, (struct sockaddr *)&dest_address, sizeof(dest_address) ) )
    {
        if(errno != EINPROGRESS)
        {
            m_errno = errno;
            g_log.Log(lv_error, "connect to other client error. fd:%d, %s:%d", m_fd, ip.c_str(), port);
            assert(false);
            return false;
        }
    }
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(m_fd, &writefds);
    int retcode = select(m_fd + 1, 0, &writefds, 0, &tv_timeout);
    if(retcode == 0 || retcode == -1)
    {
        // timeout or error.
        m_errno = errno;
        g_log.Log(lv_error, "connect to other client timeout.");
        Close();
        return false;
    }
    int connectflag;
    int flag_len = sizeof(connectflag);
    getsockopt(m_fd, SOL_SOCKET, SO_ERROR, (void*)&connectflag, (socklen_t*)&flag_len);
    if(connectflag != 0)
    {
        // connect error 
        Close();
        m_errno = errno;
        return false;
    }
    //g_log.Log(lv_debug, "client  connected, fd:%d.", m_fd);
    m_writeable = true;
    m_allow_more_send = true;
    m_isclosing = false;
    m_alive_counter = ALIVE_NUM;
    m_desthost.host_ip = ip;
    m_desthost.host_port = port;
    return true;
}

bool TcpSock::GetDestHost(std::string& ip, unsigned short int& port) const
{
    if(m_desthost.host_ip == "")
        return false;
    ip = m_desthost.host_ip;
    port = m_desthost.host_port;
    return true;
}

} }

