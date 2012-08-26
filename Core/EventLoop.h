#ifndef  CORE_NET_EVENT_LOOP_H
#define  CORE_NET_EVENT_LOOP_H

#include "lock.hpp"
#include "TcpSock.h"
#include <pthread.h>
#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <vector>

namespace core { namespace net {

class SockWaiterBase;
class EventLoop : private boost::noncopyable, public boost::enable_shared_from_this<EventLoop>
{
public:
    typedef boost::function<void()> EvTask;
    EventLoop();
    ~EventLoop();
    bool AddTcpSockToLoop(TcpSockSmartPtr sp_tcp);
    int  GetActiveTcpNum();
    void TerminateLoop();
    void SetSockWaiter(boost::shared_ptr<SockWaiterBase> spwaiter);
    bool StartLoop(pthread_t& tid);
    boost::shared_ptr<SockWaiterBase> GetEventWaiter() { return m_event_waiter; }
    //bool IsTcpExist(TcpSockSmartPtr sp_tcp);
    bool QueueTaskToLoop(EvTask task);
    bool QueueTaskToWriteLoop(EvTask task);
    bool IsInLoopThread();
    bool IsInWriteLoopThread();
    bool UpdateTcpSock(TcpSockSmartPtr sp_tcp);
    void RemoveTcpSock(TcpSockSmartPtr sp_tcp);
private:
    static void WriteLoopStartedNotify();
    void AddTcpSockToLoopInLoopThread(TcpSockSmartPtr sp_tcp);
    static void* Loop(void*);
    void CloseAllClient();
    boost::shared_ptr<SockWaiterBase>  m_event_waiter;
    volatile bool         m_terminal;
    volatile bool         m_islooprunning;
    pthread_t             m_cur_looptid;
    std::vector<EvTask>   m_pendings;
    common::locker        m_lock;
    const SockEvent       m_handle_type;
    std::string           m_write_thread_name;
    pthread_t             m_write_thread_pid;
};
} }

#endif // end of CORE_NET_EVENT_LOOP_H
