#ifndef  NETMSGBUS_REQ2RECEIVER_MGR_H
#define  NETMSGBUS_REQ2RECEIVER_MGR_H

#include "msgbus_def.h"
#include "NetMsgBusServerConnMgr.hpp"
#include "msgbus_handlerbase.hpp"
#include "condition.hpp"
#include "threadpool.h"
#include "NetMsgBusUtility.hpp"
#include "EventLoopPool.h"
#include "SelectWaiter.h"
#include "TcpSock.h"
#include "SimpleLogger.h"
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <map>
#include <string>
#include <netinet/in.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <boost/shared_array.hpp>
#include <boost/bind.hpp>

using std::string;
using namespace core::net;

#define TIMEOUT_SHORT 5
#define TIMEOUT_LONG  15
#define MAX_SENDMSG_CLIENT_NUM  1024
namespace NetMsgBus
{

struct Req2ReceiverTask
{
    Req2ReceiverTask()
        :clientname(""),
        sync(false),
        retry(false),
        timeout(TIMEOUT_LONG*4)
    {
    }
    std::string clientname;
    bool sync;
    bool retry;
    uint32_t data_len;
    boost::shared_array<char> data;
    int32_t  timeout;
};

// 本地的客户端主机信息，里面的数据都是本机字节序
typedef struct S_LocalHostInfo 
{
    S_LocalHostInfo(const std::string& ip, unsigned short int port)
        :host_ip(ip),
        host_port(port)
    {
    }
    S_LocalHostInfo()
        :host_ip(""),
        host_port(0)
    {
    }
    std::string host_ip;
    unsigned short int host_port;
} LocalHostInfo;

class Req2ReceiverMgr : public MsgHandler<Req2ReceiverMgr>
{
protected:
    Req2ReceiverMgr()
        :m_server_connmgr(NULL),
        m_req2receiver_running(false),
        m_req2receiver_terminate(false),
        sync_sessionid_(0)
    {
    }
public:
    friend class MsgHandlerMgr;
    static std::string ClassName()
    {
        return "Req2ReceiverMgr";
    }
    void InitMsgHandler()
    {
    }
    void SetServerConnMgr(ServerConnMgr* pmgr)
    {
        m_server_connmgr = pmgr;
    }

    void DisConnectFromClient(const std::string& name)
    {
        LocalHostInfo destclient;
        bool cache_exist = safe_get_cached_host_info(name, destclient);
        if(!cache_exist)
        {
            return;
        }
        if(EventLoopPool::GetEventLoop("postmsg_event_loop"))
        {
            boost::shared_ptr<SockWaiterBase> ev_waiter = EventLoopPool::GetEventLoop("postmsg_event_loop")->GetEventWaiter();
            if(ev_waiter)
            {
                // find dest host in event loop, if success , we reuse the tcp connect.
                TcpSockSmartPtr sptcp = ev_waiter->GetTcpSockByDestHost(destclient.host_ip, destclient.host_port);
                if(sptcp)
                {
                    sptcp->DisAllowSend();
                }
            }
        }
    }

    bool SendMsgDirectToClient(const std::string& clientname, uint32_t data_len, 
        boost::shared_array<char> data, string& rsp_content, int32_t timeout)
    {
        if(clientname == "" || !m_req2receiver_running )
            return false;
        Req2ReceiverTask task;
        task.clientname = clientname;
        task.data_len = data_len;
        task.data = data;
        task.sync = true;
        // 如果是同步的，那么不再向服务器请求客户端信息，直接返回失败
        task.retry = false;
        task.timeout = timeout;
        // sync sendmsg will not retry to update client info if failed to send message.
        return ProcessReqToReceiver(EventLoopPool::GetEventLoop("sendmsg_event_loop")->GetEventWaiter(), task, rsp_content);
    }

    bool PostMsgDirectToClient(const std::string& clientname, uint32_t data_len, boost::shared_array<char> data)
    {
        if( !m_req2receiver_running )
        {
            //printf("req2receiver not running when post message to receiver.\n");
            return false;
        }
        Req2ReceiverTask rtask;
        rtask.clientname = clientname;
        rtask.data = data;
        rtask.data_len = data_len;
        rtask.retry = true;
        return QueueReqTaskToReceiver(rtask);
    }

    bool Start()
    {
        if( m_req2receiver_running )
            return true;
        m_req2receiver_terminate = false;
        m_req2receiver_tid = 0;
        if(0 != pthread_create(&m_req2receiver_tid, NULL, ProcessReq2ReceiverThreadFunc, this))
        {
            perror("fatal error! req2receiver thread create failed\n");
            m_req2receiver_tid = 0;
            return false;
        }
        boost::shared_ptr<SockWaiterBase> spwaiter(new SelectWaiter());
        EventLoopPool::CreateEventLoop("sendmsg_event_loop", spwaiter);
        AddHandler("netmsgbus.server.getclient", &Req2ReceiverMgr::HandleRspGetClient, 0);
        while(!m_req2receiver_running)
        {
            usleep(100);
        }
        return true;
    }

    void Stop()
    {
        m_req2receiver_terminate = true;
        {
            core::common::locker_guard guard(m_reqtoreceiver_locker);
            m_reqtoreceiver_cond.notify_all();
        }
        if(m_req2receiver_tid)
        {
            pthread_join(m_req2receiver_tid, NULL);
        }
        RemoveAllHandlers();
        EventLoopPool::TerminateLoop("postmsg_event_loop");
        EventLoopPool::TerminateLoop("sendmsg_event_loop");
    }

private:
    bool HandleRspGetClient(const std::string& msgid, MsgBusParam& msgparam, bool& filter )
    {
        MsgBusGetClientRsp rsp;
        rsp.UnPackBody(msgparam.paramdata.get());
        if(rsp.ret_code == 0)
        {
            LocalHostInfo hostinfo;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &rsp.dest_host.server_ip, ip, sizeof(ip));
            hostinfo.host_ip = ip;
            hostinfo.host_port = ntohs(rsp.dest_host.server_port);
            std::string clientname(rsp.dest_name);
            /*printf("get client info returned. ret name : %s(%u), ip:port : %s:%d\n", clientname.c_str(), clientname.size(),
              hostinfo.host_ip.c_str(), 
              hostinfo.host_port);*/
            {
                core::common::locker_guard guard(m_cached_receiver_locker);
                m_cached_client_info[clientname] = hostinfo;
            }

            // process all the pending task belong the rsp client name.
            Req2ReceiverTaskContainerT pendingtasks;
            {
                core::common::locker_guard guard(m_waitingtask_locker);
                WaitingReq2ReceiverTaskT::iterator it = m_wait2send_task_container.find(clientname);
                if(it != m_wait2send_task_container.end())
                {
                    pendingtasks = it->second;
                    // clear all waiting tasks.
                    it->second.clear();
                    m_wait2send_task_container.erase(it);
                }
                else
                {
                    //printf("pending task %zu, but no pending task in client %s.\n", m_wait2send_task_container.size(), clientname.c_str());
                }

            }
            Req2ReceiverTaskContainerT::iterator taskit = pendingtasks.begin();
            while(taskit != pendingtasks.end())
            {
                // the client info has just update, so do not retry to update again.
                taskit->retry = false;
                QueueReqTaskToReceiver(*taskit);
                ++taskit;
            }
        }
        else
        {
            PostMsg("netmsgbus.server.getclient.error", CustomType2Param(std::string(rsp.dest_name)));
            //printf("msgbus server return error while get client info, ret_code: %d.\n", rsp.ret_code);
        }
        return true;
    }

    bool QueueReqTaskToReceiver(const Req2ReceiverTask& req_task)
    {
        core::common::locker_guard guard(m_reqtoreceiver_locker);
        m_reqtoreceiver_task_container.push_back(req_task);
        // set a condition to inform new request.
        m_reqtoreceiver_cond.notify_one();
        return true;
    }

    // 处理同步发送消息的响应数据
    size_t Req2Receiver_onRead(TcpSockSmartPtr sp_tcp, const char* pdata, size_t size)
    {
        size_t readedlen = 0;
        while(true)
        {
            size_t needlen;
            uint32_t sync_sid;
            uint32_t data_len;
            
            needlen = sizeof(sync_sid) + sizeof(data_len);
            if(size < needlen)
                return readedlen;
            sync_sid = ntohl(*((uint32_t*)pdata));
            pdata += sizeof(sync_sid);
            data_len = ntohl(*((uint32_t*)pdata));
            pdata += sizeof(data_len);
            needlen += data_len;
            if(size < needlen)
                return readedlen;
            {
                //printf("one sync data returned. sid:%u.\n", sync_sid);
                core::common::locker_guard guard(m_rsp_sendmsg_lock);
                m_sendmsg_rsp_container[sync_sid].ready = true;
                m_sendmsg_rsp_container[sync_sid].rsp_content = std::string(pdata, data_len);
                m_rsp_sendmsg_condition.notify_all();
            }
            size -= needlen;
            readedlen += needlen;
            pdata += data_len;
        }
    }

    bool Req2Receiver_onSend(TcpSockSmartPtr sp_tcp)
    {
        //printf("sendmsg data has been sended to other client.fd:%d\n", sp_tcp->GetFD());
        return true;
    }

    void Req2Receiver_onClose(TcpSockSmartPtr sp_tcp)
    {
        //core::common::locker_guard guard(m_rsp_sendmsg_lock);
        //m_sendmsg_rsp_container.erase(sp_tcp->GetFD());
        //printf("req2receiver tcp disconnected.\n");

    }
    void Req2Receiver_onError(TcpSockSmartPtr sp_tcp)
    {
        perror("sendmsg_error ");
        // you can notify the high level to handle the error, retry or just ignore.
        printf("client %d , error happened, time:%lld.\n", sp_tcp->GetFD(), (int64_t)utility::GetTickCount());
    }
    bool IdentiySelfToReceiver(TcpSockSmartPtr sp_tcp)
    {
        Req2ReceiverTask identifytask;
        identifytask.sync = 0;
        std::string netmsg_str;
        string sendername;
        assert(m_server_connmgr != NULL);
        if(m_server_connmgr)
        {
            sendername = m_server_connmgr->GetClientName();
        }
        EncodeMsgKeyValue(sendername);
        netmsg_str = "msgsender=" + sendername;
        uint32_t netmsg_len = netmsg_str.size();
        boost::shared_array<char> netmsg_data(new char[netmsg_len]);
        memcpy(netmsg_data.get(), netmsg_str.data(), netmsg_len);
        identifytask.data = netmsg_data;
        identifytask.data_len = netmsg_len;
        string rsp;
        return WriteTaskDataToReceiver(sp_tcp, identifytask, rsp);
    }
    bool WriteTaskDataToReceiver(TcpSockSmartPtr sp_tcp, const Req2ReceiverTask& task, string& rsp_content)
    {
        char syncflag = 0;
        uint32_t waiting_syncid = 0;
        if(task.sync)
        {
            //g_log.Log(lv_debug, "begin send sync data to client:%lld\n", (int64_t)core::utility::GetTickCount());
            syncflag = 1;
            core::common::locker_guard guard(m_rsp_sendmsg_lock);
            ++sync_sessionid_;
            waiting_syncid = sync_sessionid_;
            m_sendmsg_rsp_container[waiting_syncid].ready = false;
            m_sendmsg_rsp_container[waiting_syncid].rsp_content = ""; 
        }
        uint32_t write_len = sizeof(syncflag) + sizeof(waiting_syncid) + sizeof(task.data_len) + task.data_len;
        boost::shared_array<char> writedata(new char[write_len]);
        memcpy(writedata.get(), &syncflag, sizeof(syncflag));
        uint32_t waiting_syncid_n = htonl(waiting_syncid);
        uint32_t data_len_n = htonl(task.data_len);
        memcpy(writedata.get() + sizeof(syncflag), &waiting_syncid_n, sizeof(waiting_syncid_n));
        memcpy(writedata.get() + sizeof(syncflag) + sizeof(waiting_syncid), (char*)&data_len_n, sizeof(data_len_n));
        memcpy(writedata.get() + sizeof(syncflag) + sizeof(waiting_syncid) + sizeof(task.data_len), task.data.get(), task.data_len);
        if(!sp_tcp->SendData(writedata.get(), write_len))
        {
            printf("send msg to other client failed.\n");
            rsp_content = "send data failed.";
            return false;
        }
        // 如果要求同步发送， 则等待
        bool result = true;
        if(task.sync)
        {
            struct timespec ts;
            ts.tv_sec = time(NULL) + task.timeout;
            ts.tv_nsec = 0;
            bool ready = false;
            while(!ready)
            {
                {
                    core::common::locker_guard guard(m_rsp_sendmsg_lock);
                    ready = m_sendmsg_rsp_container[waiting_syncid].ready;
                    //printf("one sync data waiter wakeup. sid:%d, ready:%d.\n", waiting_syncid, ready?1:0);
                    if(ready)
                    {
                        rsp_content = m_sendmsg_rsp_container[waiting_syncid].rsp_content;
                        break;
                    }
                    //printf("sid:%d sendmsg wake up for ready.\n", waiting_syncid);
                    int retcode = m_rsp_sendmsg_condition.waittime(m_rsp_sendmsg_lock, &ts);
                    if(retcode == ETIMEDOUT)
                    {
                        printf(" wakeup for timeout. sid:%d.\n", waiting_syncid);
                        //return false;
                        result = false;
                        rsp_content = "wait time out.";
                        break;
                    }
                }
            }
            // close sync Tcp, remove the tcp in onClose event.
            // changed: do not close for later reuse, now sync request can be seperate by sessionid 
            // in the same tcp.
            // sp_tcp->DisAllowSend();
            //g_log.Log(lv_debug, "end send sync data to client:%lld\n", (int64_t)core::utility::GetTickCount());
            core::common::locker_guard guard(m_rsp_sendmsg_lock);
            m_sendmsg_rsp_container.erase(waiting_syncid);
        }
        return result;
    }

    // 处理特定的到某个客户端的请求,retry stand for if failed to send the data , whether to update the client host info and resend the data.
    bool ProcessReqToReceiver(boost::shared_ptr<SockWaiterBase> ev_waiter, const Req2ReceiverTask& task, string& rsp_content)
    {
        LocalHostInfo destclient;
        bool cache_exist = safe_get_cached_host_info(task.clientname, destclient);
        if(!cache_exist)
        {
            // 缓存中没有该客户端信息，先将该任务放入等待列表中,
            // 然后向服务器获取客户端主机信息
            // 一旦收到服务器回来的客户端信息，会处理对应客户端的等待的任务
            if(task.retry)
            {
                safe_queue_waiting_task(task);
                if(m_server_connmgr)
                    m_server_connmgr->ReqReceiverInfo(task.clientname);
            }
            else
            {
                printf("error send data to client, no cached client info.\n");
            }
            rsp_content = "error send data to client, no cached client info.";
            return false;
        }
        if(ev_waiter == NULL)
        {
            printf("error: NULL event waiter.\n");
            return false;
        }
        TcpSockSmartPtr newtcp;
        // changed: do not use tcp fd to seperate different sync request, use a sync_sessionid_ instead,
        // so we can reuse the old tcp connection.
        //if(task.sync)
        //{
        //    // sync get data , we will create new tcp for each sync task.
        //}
        //else
        //{
        //    // find dest host in event loop, if success , we reuse the tcp connect.
        //    newtcp = ev_waiter->GetTcpSockByDestHost(destclient.host_ip, destclient.host_port);
        //}

        // find dest host in event loop, if success , we reuse the tcp connect.
        newtcp = ev_waiter->GetTcpSockByDestHost(destclient.host_ip, destclient.host_port);
        if( !newtcp )
        {
            //printf("begin connect client:%lld\n", (int64_t)core::utility::GetTickCount());
            //printf("begin connect client: %s,%u. \n", task.clientname.c_str(), destclient.host_port);
            struct timeval tv;
            tv.tv_sec = task.timeout;
            tv.tv_usec = 0;
            newtcp.reset(new TcpSock());
            // 连接指定客户端并发送数据 
            bool connected = newtcp->Connect(destclient.host_ip, destclient.host_port, tv);
            if ( !connected )
            {
                // 连接失败很可能是缓存的信息已经失效，因此我们把它加到等待列表中，并向服务器请求新的信息
                if(task.retry)
                {
                    safe_queue_waiting_task(task);
                    safe_remove_cached_host_info(task.clientname);
                    if(m_server_connmgr)
                        m_server_connmgr->ReqReceiverInfo(task.clientname);
                }
                else
                {
                    perror("error connect to receiver client.\n");
                    PostMsg("netmsgbus.client.connectreceiver.failed", CustomType2Param(task.clientname));
                }
                rsp_content = "error connect to receiver client.";
                return false;
            }
            newtcp->SetNonBlock();
            newtcp->SetCloseAfterExec();
            SockHandler callback;
            if(task.sync)
            {
                // sync sendmsg need a onRead callback to get the sync rsp data.
                callback.onRead = boost::bind(&Req2ReceiverMgr::Req2Receiver_onRead, this, _1, _2, _3);
            }
            else
            {
                // do not need the onRead event.
                callback.onRead = NULL;
            }
            callback.onSend = boost::bind(&Req2ReceiverMgr::Req2Receiver_onSend, this, _1);
            callback.onClose = boost::bind(&Req2ReceiverMgr::Req2Receiver_onClose, this, _1);
            callback.onError = boost::bind(&Req2ReceiverMgr::Req2Receiver_onError, this, _1);
            newtcp->SetSockHandler(callback);

            ev_waiter->AddTcpSock(newtcp);
            
            // first identify me to the receiver.
            IdentiySelfToReceiver(newtcp);
        }
        if(WriteTaskDataToReceiver(newtcp, task, rsp_content))
        {
            return true;
        }
        return false;
    }

    // 不关心返回值的处理到客户端请求
    void ProcessReqToReceiver(boost::shared_ptr<SockWaiterBase> ev_waiter, const Req2ReceiverTask& task)
    {
        std::string tmp;
        ProcessReqToReceiver(ev_waiter, task, tmp);
    }

    // 专门用于处理向其它客户端异步的发送消息
    static void * ProcessReq2ReceiverThreadFunc(void * param)
    {
        Req2ReceiverMgr * req2recv_mgr = (Req2ReceiverMgr*)param;
        if( req2recv_mgr == NULL )
        {
            assert(0);
            return 0;
        }
        req2recv_mgr->m_req2receiver_running = true;
        boost::shared_ptr<SockWaiterBase> spwaiter(new SelectWaiter());
        EventLoopPool::CreateEventLoop("postmsg_event_loop", spwaiter);
        while(true)
        {
            Req2ReceiverTask rtask;
            {
                // lock 
                core::common::locker_guard guard(req2recv_mgr->m_reqtoreceiver_locker);
                while (req2recv_mgr->m_reqtoreceiver_task_container.empty())
                {
                    // even if the msgbus server is down, we can still use the cached info to sendmsg to receiver.
                    if( req2recv_mgr->m_req2receiver_terminate)
                    {
                        req2recv_mgr->m_req2receiver_running = false;
                        return 0;
                    }
                    // wait request event.
                    //printf("waiting the request task to client.\n");
                    req2recv_mgr->m_reqtoreceiver_cond.wait(req2recv_mgr->m_reqtoreceiver_locker);
                }
                rtask = req2recv_mgr->m_reqtoreceiver_task_container.front();
                req2recv_mgr->m_reqtoreceiver_task_container.pop_front();
            }
            //threadpool::queue_work_task(boost::bind(&Req2ReceiverMgr::ProcessReqToReceiver, req2recv_mgr, spwaiter, rtask), 0);
            // in order to make sure the order of sendmsg , we should use the same thread to process the same client name sendmsg.
            threadpool::queue_work_task_to_named_thread(boost::bind(&Req2ReceiverMgr::ProcessReqToReceiver, req2recv_mgr, spwaiter, rtask),
                "ProcessReqToReceiver" + rtask.clientname);
        }
        req2recv_mgr->m_req2receiver_running = false;
        return 0;
    }
private:
    bool safe_queue_waiting_task(const Req2ReceiverTask& req_task)
    {
        core::common::locker_guard guard(m_waitingtask_locker);
        m_wait2send_task_container[req_task.clientname].push_back(req_task);
        return true;
    }

    bool safe_get_cached_host_info(const std::string& clientname, LocalHostInfo& hostinfo)
    {
        core::common::locker_guard guard(m_cached_receiver_locker);
        LocalHostContainerT::const_iterator cit = m_cached_client_info.find(clientname);
        if(cit != m_cached_client_info.end())
        {
            hostinfo = cit->second;
            return true;
        }
        return false;
    }

    void safe_remove_cached_host_info(const std::string& clientname)
    {
        core::common::locker_guard guard(m_cached_receiver_locker);
        m_cached_client_info.erase(clientname);
    }

    pthread_t m_req2receiver_tid;

    typedef std::deque< Req2ReceiverTask > Req2ReceiverTaskContainerT;
    Req2ReceiverTaskContainerT m_reqtoreceiver_task_container;

    //EventLoopPool m_evpool;

    core::common::condition m_reqtoreceiver_cond;
    core::common::locker    m_reqtoreceiver_locker;

    typedef std::map< std::string, LocalHostInfo > LocalHostContainerT;
    typedef std::map< std::string, Req2ReceiverTaskContainerT > WaitingReq2ReceiverTaskT;
    // 一些等待客户端IP信息返回的任务队列，GetClient返回后一起发出去
    WaitingReq2ReceiverTaskT m_wait2send_task_container;
    LocalHostContainerT m_cached_client_info;

    core::common::locker    m_cached_receiver_locker;
    core::common::locker    m_waitingtask_locker;

    core::common::locker    m_rsp_sendmsg_lock;
    core::common::condition m_rsp_sendmsg_condition;
    struct RspSendMsgState
    {
        bool ready;
        std::string rsp_content;
    };
    std::map< int, RspSendMsgState > m_sendmsg_rsp_container;

    ServerConnMgr* m_server_connmgr;
    volatile bool m_req2receiver_running;
    volatile bool m_req2receiver_terminate;
    uint32_t  sync_sessionid_;
};

DECLARE_SP_PTR(Req2ReceiverMgr);

}
#endif // end of NETMSGBUS_REQ2RECEIVER_MGR_H
