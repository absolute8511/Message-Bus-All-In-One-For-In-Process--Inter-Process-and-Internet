#ifndef  CORE_NET_SOCK_HANDLER_H
#define  CORE_NET_SOCK_HANDLER_H

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

namespace core { namespace net {

class TcpSock;
typedef boost::shared_ptr< TcpSock > TcpSockSmartPtr;

typedef boost::function<void(TcpSockSmartPtr)> onCloseCB;
typedef boost::function<size_t(TcpSockSmartPtr, const char*, size_t)> onReadCB;
// onsend return true to wait for next sending, or to close the fd for sending data. return false will close the fd,
// so you should not write any data to the fd again.
typedef boost::function<bool(TcpSockSmartPtr)> onSendCB;
typedef boost::function<void(TcpSockSmartPtr)> onErrorCB;
typedef boost::function<void(TcpSockSmartPtr)> onTimeoutCB;
struct SockHandler
{
    onCloseCB onClose;
    onReadCB  onRead;
    onSendCB  onSend;
    onErrorCB onError;
    onTimeoutCB onTimeout;
    SockHandler()
        :onClose(NULL),
        onRead(NULL),
        onSend(NULL),
        onError(NULL),
        onTimeout(NULL)
    {
    }
};

} }

#endif // end of CORE_NET_SOCK_HANDLER_H
