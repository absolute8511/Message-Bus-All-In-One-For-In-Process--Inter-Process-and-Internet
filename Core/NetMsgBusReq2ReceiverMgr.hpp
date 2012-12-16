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
#include "TcpClientPool.h"
#include "NetMsgBusFuture.hpp"
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <map>
#include <boost/unordered_map.hpp>
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
#define CLIENT_POOL_SIZE  4

namespace NetMsgBus
{
// client host info, all data with local byte order.
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
    uint32_t future_id;   // note: 0 is reserved for non-future rsp need.
    bool retry;
    uint32_t data_len;
    boost::shared_array<char> data;
    int32_t  timeout;
    LocalHostInfo  dest_client;
};

class Req2ReceiverMgr : public MsgHandler<Req2ReceiverMgr>
{
protected:
    Req2ReceiverMgr()
        :m_server_connmgr(NULL),
        m_req2receiver_running(false),
        m_req2receiver_terminate(false),
        future_sessionid_(0)
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

    //void DisConnectFromClient(const std::string& name)
    //{
    //    LocalHostInfo destclient;
    //    bool cache_exist = safe_get_cached_host_info(name, destclient);
    //    if(!cache_exist)
    //    {
    //        return;
    //    }
    //    if(EventLoopPool::GetEventLoop("postmsg_event_loop"))
    //    {
    //        // find dest host in event loop, if success , we reuse the tcp connect.
    //        TcpSockSmartPtr sptcp = m_postmsg_client_conn_pool.GetTcpSockByDestHost(destclient.host_ip, destclient.host_port);
    //        if(sptcp)
    //        {
    //            sptcp->DisAllowSend();
    //        }
    //    }
    //}

    bool SendMsgDirectToClient(const std::string& dest_ip, unsigned short dest_port, uint32_t data_len, 
        boost::shared_array<char> data, string& rsp_content, int32_t timeout)
    {
        if(dest_ip.empty() || !m_req2receiver_running )
            return false;
        Req2ReceiverTask task;
        task.data_len = data_len;
        task.data = data;
        task.sync = true;
        task.retry = false;
        task.timeout = timeout;
        task.dest_client.host_ip = dest_ip;
        task.dest_client.host_port = dest_port;
        std::pair<uint32_t, boost::shared_ptr<NetFuture> > future = safe_insert_future();
        task.future_id = future.first;
        // sync sendmsg will not retry to update client info if failed to send message.
        return ProcessReqToReceiver(boost::shared_ptr<SockWaiterBase>(), task, rsp_content);
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
        std::pair<uint32_t, boost::shared_ptr<NetFuture> > future = safe_insert_future();
        task.future_id = future.first;
        // sync sendmsg will not retry to update client info if failed to send message.
        return ProcessReqToReceiver(boost::shared_ptr<SockWaiterBase>(), task, rsp_content);
    }

    boost::shared_ptr<NetFuture> PostMsgDirectToClient(const std::string& dest_ip, unsigned short dest_port,
        uint32_t data_len, boost::shared_array<char> data)
    {
        boost::shared_ptr<NetFuture> ret_future;
        if( !m_req2receiver_running )
        {
            LOG(g_log, lv_debug, "req2receiver not running when post message to receiver.");
            return ret_future;
        }
        if(dest_ip.empty())
            return ret_future;

        Req2ReceiverTask rtask;
        rtask.data = data;
        rtask.data_len = data_len;
        // send by ip , no retry need.
        rtask.retry = false;
        rtask.dest_client.host_ip = dest_ip;
        rtask.dest_client.host_port = dest_port;
        std::pair<uint32_t, boost::shared_ptr<NetFuture> > future = safe_insert_future();
        rtask.future_id = future.first;
        if(!QueueReqTaskToReceiver(rtask))
            return boost::shared_ptr<NetFuture>();
        return future.second;
    }

    boost::shared_ptr<NetFuture> PostMsgDirectToClient(const std::string& clientname, uint32_t data_len, boost::shared_array<char> data)
    {
        if( !m_req2receiver_running )
        {
            LOG(g_log, lv_debug, "req2receiver not running when post message to receiver.");
            return boost::shared_ptr<NetFuture>();
        }
        Req2ReceiverTask rtask;
        rtask.clientname = clientname;
        rtask.data = data;
        rtask.data_len = data_len;
        rtask.retry = true;
        std::pair<uint32_t, boost::shared_ptr<NetFuture> > future = safe_insert_future();
        rtask.future_id = future.first;
        if(!QueueReqTaskToReceiver(rtask))
            return boost::shared_ptr<NetFuture>();
        return future.second;
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
        AddHandler("netmsgbus.server.getclient", &Req2ReceiverMgr::HandleRspGetClient, 0);
        while(!m_req2receiver_running)
        {
            usleep(100);
        }
        return true;
    }

    void Stop()
    {
        safe_clear_bad_future();
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
    }

private:
    bool HandleRspGetClient(const std::string& msgid, MsgBusParam& msgparam, bool& filter )
    {
        MsgBusGetClientRsp rsp;
        rsp.UnPackBody(msgparam.paramdata.get());
        std::string clientname(rsp.dest_name);
        // remove all the pending task belong the rsp client name.
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
                LOG(g_log, lv_info, "pending task %zu, but no pending task in client %s.\n", m_wait2send_task_container.size(), clientname.c_str());
            }
        }
        Req2ReceiverTaskContainerT::iterator taskit = pendingtasks.begin();

        if(rsp.ret_code == 0)
        {
            LocalHostInfo hostinfo;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &rsp.dest_host.server_ip, ip, sizeof(ip));
            hostinfo.host_ip = ip;
            hostinfo.host_port = ntohs(rsp.dest_host.server_port);
            LOG(g_log, lv_debug, "get client info returned. ret name : %s(%u), ip:port : %s:%d\n", clientname.c_str(), clientname.size(),
              hostinfo.host_ip.c_str(), 
              hostinfo.host_port);
            {
                core::common::locker_guard guard(m_cached_receiver_locker);
                m_cached_client_info[clientname] = hostinfo;
            }
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
            LOG(g_log, lv_warn, "server return error while query client info, ret_code: %d.", rsp.ret_code);
            while(taskit != pendingtasks.end())
            {
                taskit->retry = false;
                safe_remove_future(taskit->future_id);
                ++taskit;
            }
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
            uint32_t future_sid;
            uint32_t data_len;
            
            needlen = sizeof(future_sid) + sizeof(data_len);
            if(size < needlen)
                return readedlen;
            future_sid = ntohl(*((uint32_t*)pdata));
            pdata += sizeof(future_sid);
            data_len = ntohl(*((uint32_t*)pdata));
            pdata += sizeof(data_len);
            needlen += data_len;
            if(size < needlen)
                return readedlen;
            boost::shared_ptr<NetFuture> ready_sendmsg_rsp;
            {
                //g_log.Log(lv_debug, "receiver future data returned to client:%lld, sid:%u, fd:%d\n", (int64_t)core::utility::GetTickCount(), future_sid, sp_tcp->GetFD());
                core::common::locker_guard guard(m_rsp_sendmsg_lock);
                boost::unordered_map<uint32_t, boost::shared_ptr<NetFuture> >::iterator future_it = m_sendmsg_rsp_container.find(future_sid);
                if(future_it == m_sendmsg_rsp_container.end())
                {
                    g_log.Log(lv_warn, "received a non-exist future_sid rsp, sid:%u ", future_sid);
                }
                else
                {
                    ready_sendmsg_rsp = future_it->second;
                    m_sendmsg_rsp_container.erase(future_it);
                }
            }
            if(ready_sendmsg_rsp)
            {
                ready_sendmsg_rsp->set_result(pdata, data_len);
            }
            size -= needlen;
            readedlen += needlen;
            pdata += data_len;
        }
    }

    bool Req2Receiver_onSend(TcpSockSmartPtr sp_tcp)
    {
        return true;
    }

    void Req2Receiver_onClose(TcpSockSmartPtr sp_tcp)
    {
        //printf("req2receiver tcp disconnected.\n");
        m_sendmsg_client_conn_pool.RemoveTcpSock(sp_tcp);
        m_postmsg_client_conn_pool.RemoveTcpSock(sp_tcp);
        safe_clear_bad_future();
    }
    void Req2Receiver_onError(TcpSockSmartPtr sp_tcp)
    {
        // you can notify the high level to handle the error, retry or just ignore.
        LOG(g_log, lv_error, "client %d , error happened, time:%lld.\n", sp_tcp->GetFD(), (int64_t)utility::GetTickCount());
        m_sendmsg_client_conn_pool.RemoveTcpSock(sp_tcp);
        m_postmsg_client_conn_pool.RemoveTcpSock(sp_tcp);
        safe_clear_bad_future();
    }
    bool IdentiySelfToReceiver(TcpSockSmartPtr sp_tcp)
    {
        Req2ReceiverTask identifytask;
        identifytask.sync = 0;
        string sendername;
        if(m_server_connmgr)
        {
            sendername = m_server_connmgr->GetClientName();
        }
        MsgBusParam netmsg_param;
        GenerateNetMsgContent("", netmsg_param, sendername, netmsg_param);
        identifytask.future_id = 0;
        identifytask.data = netmsg_param.paramdata;
        identifytask.data_len = netmsg_param.paramlen;
        string rsp;
        return WriteTaskDataToReceiver(sp_tcp, identifytask, rsp);
    }
    bool WriteTaskDataToReceiver(TcpSockSmartPtr sp_tcp, const Req2ReceiverTask& task, string& rsp_content)
    {
        char syncflag = 0;
        uint32_t waiting_futureid = task.future_id;
        boost::shared_ptr<NetFuture> cur_sendmsg_rsp;
        if(task.sync)
        {
            syncflag = 1;
            cur_sendmsg_rsp = safe_get_future(waiting_futureid);
        }
        g_log.Log(lv_debug, "begin send data to receiver:%lld, sid:%u, datalen:%d, fd:%d", (int64_t)core::utility::GetTickCount(),
            task.future_id, 9 + task.data_len, sp_tcp->GetFD());
        uint32_t write_len = sizeof(syncflag) + sizeof(waiting_futureid) + sizeof(task.data_len) + task.data_len;
        boost::shared_array<char> writedata(new char[write_len]);
        memcpy(writedata.get(), &syncflag, sizeof(syncflag));
        uint32_t waiting_futureid_n = htonl(waiting_futureid);
        uint32_t data_len_n = htonl(task.data_len);
        memcpy(writedata.get() + sizeof(syncflag), &waiting_futureid_n, sizeof(waiting_futureid_n));
        memcpy(writedata.get() + sizeof(syncflag) + sizeof(waiting_futureid), (char*)&data_len_n, sizeof(data_len_n));
        memcpy(writedata.get() + sizeof(syncflag) + sizeof(waiting_futureid) + sizeof(task.data_len), task.data.get(), task.data_len);
        if(!sp_tcp->SendData(writedata.get(), write_len))
        {
            LOG(g_log, lv_warn, "send msg to other client failed.");
            rsp_content = "send data failed.";
            safe_remove_future(waiting_futureid);
            return false;
        }
        // 如果要求同步发送， 则等待
        bool result = true;
        if(task.sync)
        {
            if(!cur_sendmsg_rsp)
            {
                LOG(g_log, lv_warn, "get future session failed: %u.", waiting_futureid);
                result = false;
            }
            else
                result = cur_sendmsg_rsp->get(task.timeout, rsp_content);
            //g_log.Log(lv_debug, "end send sync data to client:%lld, sid:%u\n", (int64_t)core::utility::GetTickCount(), waiting_futureid);
            // future will erase in on-read event.
            //core::common::locker_guard guard(m_rsp_sendmsg_lock);
            //m_sendmsg_rsp_container.erase(waiting_futureid);
        }
        return result;
    }

    // 处理特定的到某个客户端的请求,retry stand for if failed to send the data , whether to update the client host info and resend the data.
    bool ProcessReqToReceiver(boost::shared_ptr<SockWaiterBase> ev_waiter, const Req2ReceiverTask& task, string& rsp_content)
    {
        LocalHostInfo destclient;
        if(task.clientname.empty())
        {
            assert(!task.dest_client.host_ip.empty());
            destclient = task.dest_client;
        }
        else
        {
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
                    LOG(g_log, lv_warn, "error send data to client, no cached client info.");
                    safe_remove_future(task.future_id);
                    return false;
                }
                return true;
            }
        }
        TcpSockSmartPtr newtcp;

        TcpClientPool* pclient_conn_pool = &m_postmsg_client_conn_pool;
        // changed: do not use tcp fd to seperate different sync request, use a future_sessionid_ instead,
        // so we can reuse the old tcp connection.
        if(task.sync)
        {
            pclient_conn_pool = &m_sendmsg_client_conn_pool;
        }
        newtcp = pclient_conn_pool->GetTcpSockByDestHost(destclient.host_ip, destclient.host_port);
        if( !newtcp )
        {
            SockHandler callback;
            // both sync sendmsg and postmsg need a onRead callback to get the future rsp data.
            callback.onRead = boost::bind(&Req2ReceiverMgr::Req2Receiver_onRead, this, _1, _2, _3);
            callback.onSend = boost::bind(&Req2ReceiverMgr::Req2Receiver_onSend, this, _1);
            callback.onClose = boost::bind(&Req2ReceiverMgr::Req2Receiver_onClose, this, _1);
            callback.onError = boost::bind(&Req2ReceiverMgr::Req2Receiver_onError, this, _1);

            std::string loopname;
            if(ev_waiter == NULL)
            {
                g_log.Log(lv_info, "NULL event waiter. use the default netmsgbus eventloop in the pool.");
                loopname = NETMSGBUS_EVLOOP_NAME;
            }
            else
            {
                loopname = "postmsg_event_loop";
            }
            // first identify me to the receiver.
            //IdentiySelfToReceiver(newtcp);
            bool ret;
            ret = pclient_conn_pool->CreateTcpSock(loopname, destclient.host_ip, destclient.host_port, CLIENT_POOL_SIZE, 
                task.timeout, callback, boost::bind(&Req2ReceiverMgr::IdentiySelfToReceiver, this, _1));
            if(!ret)
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
                    safe_remove_future(task.future_id);
                    return false;
                }
                return true;
            }
            newtcp = pclient_conn_pool->GetTcpSockByDestHost(destclient.host_ip, destclient.host_port);
            if(!newtcp)
            {
                safe_remove_future(task.future_id);
                return false;
            }
        }
        if(WriteTaskDataToReceiver(newtcp, task, rsp_content))
        {
            return true;
        }
        safe_remove_future(task.future_id);
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
            std::string thread_name;
            if(rtask.clientname.size() <= 2)
            {
                thread_name = "byip";
            }
            else
            {
                thread_name = rtask.clientname.substr(rtask.clientname.size() - 2);
            }
            // in order to make sure the order of sendmsg , we should use the same thread to process the same client name sendmsg.
            threadpool::queue_work_task_to_named_thread(boost::bind(&Req2ReceiverMgr::ProcessReqToReceiver, req2recv_mgr, spwaiter, rtask),
                "ProcessReqToReceiver" + thread_name);
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

    std::pair<uint32_t, boost::shared_ptr<NetFuture> > safe_insert_future()
    {
        core::common::locker_guard guard(m_rsp_sendmsg_lock);
        ++future_sessionid_;
        if(future_sessionid_ == 0)
            ++future_sessionid_;
        uint32_t waiting_futureid = future_sessionid_;
        std::pair<FutureRspContainerT::iterator, bool> inserted = m_sendmsg_rsp_container.insert(
            std::make_pair(waiting_futureid, boost::shared_ptr<NetFuture>(new NetFuture)));
        assert(inserted.second);
        return std::make_pair(waiting_futureid, inserted.first->second);
    }

    boost::shared_ptr<NetFuture> safe_get_future(uint32_t future_sid)
    {
        boost::shared_ptr<NetFuture> future;
        core::common::locker_guard guard(m_rsp_sendmsg_lock);
        FutureRspContainerT::iterator future_it = m_sendmsg_rsp_container.find(future_sid);
        if(future_it != m_sendmsg_rsp_container.end())
        {
            future = future_it->second;
        }
        return future;
    }
    void safe_remove_future(uint32_t future_sid)
    {
        core::common::locker_guard guard(m_rsp_sendmsg_lock);
        m_sendmsg_rsp_container.erase(future_sid);
    }

    void safe_clear_bad_future()
    {
        core::common::locker_guard guard(m_rsp_sendmsg_lock);
        FutureRspContainerT::iterator future_it = m_sendmsg_rsp_container.begin();
        while(future_it != m_sendmsg_rsp_container.end())
        {
            if(future_it->second && future_it->second->is_bad())
            {
                future_it = m_sendmsg_rsp_container.erase(future_it);
            }
            else
            {
                ++future_it;
            }
        }
    }

    pthread_t m_req2receiver_tid;

    typedef std::deque< Req2ReceiverTask > Req2ReceiverTaskContainerT;
    Req2ReceiverTaskContainerT m_reqtoreceiver_task_container;

    core::common::condition m_reqtoreceiver_cond;
    core::common::locker    m_reqtoreceiver_locker;

    typedef boost::unordered_map< std::string, LocalHostInfo > LocalHostContainerT;
    typedef boost::unordered_map< std::string, Req2ReceiverTaskContainerT > WaitingReq2ReceiverTaskT;
    // 一些等待客户端IP信息返回的任务队列，GetClient返回后一起发出去
    WaitingReq2ReceiverTaskT m_wait2send_task_container;
    LocalHostContainerT m_cached_client_info;

    core::common::locker    m_cached_receiver_locker;
    core::common::locker    m_waitingtask_locker;

    core::common::locker    m_rsp_sendmsg_lock;
    typedef boost::unordered_map< uint32_t, boost::shared_ptr<NetFuture> > FutureRspContainerT;
    FutureRspContainerT m_sendmsg_rsp_container;

    ServerConnMgr* m_server_connmgr;
    volatile bool m_req2receiver_running;
    volatile bool m_req2receiver_terminate;
    uint32_t  future_sessionid_;
    TcpClientPool  m_sendmsg_client_conn_pool;
    TcpClientPool  m_postmsg_client_conn_pool;
};

DECLARE_SP_PTR(Req2ReceiverMgr);

}
#endif // end of NETMSGBUS_REQ2RECEIVER_MGR_H
