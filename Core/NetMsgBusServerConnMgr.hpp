#ifndef  NETMSGBUS_SERVER_CONNMGR
#define  NETMSGBUS_SERVER_CONNMGR

#include "NetMsgBusUtility.hpp"
#include "EventLoopPool.h"

#if defined (__APPLE__) || defined (__MACH__) 
#include "SelectWaiter.h"
#else
#include "EpollWaiter.h"
#endif

#include "TcpSock.h"
#include "CommonUtility.hpp"
#include "SimpleLogger.h"
#include "NetMsgBus.PBParam.pb.h"
#include <google/protobuf/descriptor.h>
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
using namespace core;

#define SERVER_CALL_TIMEOUT 5

namespace NetMsgBus 
{

class PBHandlerBase
{
public:
    virtual ~PBHandlerBase(){};
    virtual void onPbData(uint32_t msg_id, const string& pbtype, const string& pbdata) const = 0;
};

template <typename T> class PBHandlerT: public PBHandlerBase
{
public:
    typedef boost::function<void(uint32_t msg_id, T*)> PBHandlerCB;
    PBHandlerT(const PBHandlerCB& cb)
        :cb_(cb)
    {
    }
    virtual void onPbData(uint32_t msg_id, const string& pbtype, const string& pbdata) const
    {
        const google::protobuf::DescriptorPool* pool = google::protobuf::DescriptorPool::generated_pool();
        const google::protobuf::Descriptor* desp = pool->FindMessageTypeByName(pbtype);
        const google::protobuf::Message* prototype = google::protobuf::MessageFactory::generated_factory()->GetPrototype(desp);
        if(prototype)
        {
            google::protobuf::Message* msg = prototype->New();
            if(msg->ParseFromArray(pbdata.data(), pbdata.size()))
            {
                T *t = dynamic_cast<T*>(msg);
                assert(cb_ != NULL);
                cb_(msg_id, t);
            }
        }
    }
private:
    PBHandlerCB cb_;
};

typedef std::map<string, boost::shared_ptr<PBHandlerBase> > PBHandlerContainerT;

// the server connection of the local message receiver client, each receiver will hold one 
// server connection to communicate some control message with the message bus server.
class ServerConnMgr
{
public:
    ServerConnMgr()
        :m_server_connecting(false),
        m_isreceiver_registered(false),
        g_log("ServerConnMgr")
    {
        m_allrsphandlers[RSP_REGISTER] = &ServerConnMgr::HandleRspRegister;
        m_allrsphandlers[RSP_GETCLIENT] = &ServerConnMgr::HandleRspGetClient;
        m_allrsphandlers[RSP_SENDMSG] = &ServerConnMgr::HandleRspSendMsg;
        m_allrsphandlers[RSP_UNREGISTER] = &ServerConnMgr::HandleRspUnRegister;
        m_allrsphandlers[REQ_SENDMSG] = &ServerConnMgr::HandleReqSendMsg;
        m_allrsphandlers[RSP_CONFIRM_ALIVE] = &ServerConnMgr::HandleRspConfirmAlive;
        m_allrsphandlers[BODY_PBTYPE] = &ServerConnMgr::HandleRspPBBody;
        regist_pbdata_handler<NetMsgBus::PBQueryServicesRsp>(boost::bind(&ServerConnMgr::HandleQueryServicesRsp, this, _1, _2));
    }
    ~ServerConnMgr()
    {
    }
    std::string GetClientName()
    {
        return m_receiver_name;
    }
    bool onServerSendReady(TcpSockSmartPtr sp_tcp)
    {
        boost::shared_array<char> param(new char[1]);
        param[0] = '\0';
        PostMsg("netmsgbus.server.reqdata.sendfinished", param, 1);
        return true;
    }

    size_t ServerRspProcess(TcpSockSmartPtr sp_tcp, const char* pdata, size_t size)
    {
        size_t readedlen = 0;
        while(true)
        {
            size_t needlen;
            needlen = sizeof(MsgBusPackHead);
            if(size < needlen)
            {
                return readedlen;
            }
            MsgBusPackHead head;
            if (-1 == head.UnPackHead(pdata))
            {
                printf("unpack head error.\n");
                return size;
            }
            pdata += sizeof(MsgBusPackHead);
            needlen += head.body_len;
            if(size < needlen )
            {
                return readedlen;
            }
            std::string server_rsp_body(pdata, head.body_len);
            ProcessRspBody(head.msg_id, head.body_type, server_rsp_body);

            size -= needlen;
            readedlen += needlen;
            pdata += head.body_len;
        }
    }
    void onServerTcpClose(TcpSockSmartPtr sp_tcp)
    {
        m_server_connecting = false;
        m_server_tcp.reset();
        g_log.Log(lv_debug, "disconnect from msgbus server for closed.");
    }
    void onServerTcpError(TcpSockSmartPtr)
    {
        g_log.Log(lv_error, "disconnect from msgbus server for error.");
        m_server_connecting = false;
        m_server_tcp.reset();
    }
    void onServerTimeout(TcpSockSmartPtr sp_tcp)
    {
        g_log.Log(lv_debug, "sending heart to msgbus server for timeout.");
        MsgBusConfirmAliveReq req;
        req.alive_flag = 0;
        boost::shared_array<char> buf(new char[req.Size()]);
        req.PackData(buf.get());
        if(sp_tcp)
            sp_tcp->SendData(buf.get(), req.Size());
    }
    bool StartServerCommunicateLoop(const std::string& serverip, unsigned short int serverport)
    {
        if(m_server_connecting)
            return true;

        // 设置消息总线中心服务器的地址 
        struct timeval tv;
        tv.tv_sec = 15;
        tv.tv_usec = 0;
        m_serverip = serverip;
        m_serverport = serverport;
        m_server_tcp.reset(new TcpSock());
        if(!m_server_tcp->Connect(m_serverip, m_serverport, tv))
        {
            g_log.Log(lv_error, "client connect to server error.");
            return false;
        }
        m_server_connecting = true;
        SockHandler callback;
        callback.onRead = boost::bind(&ServerConnMgr::ServerRspProcess, this, _1, _2, _3);
        callback.onSend = boost::bind(&ServerConnMgr::onServerSendReady, this, _1);
        callback.onClose = boost::bind(&ServerConnMgr::onServerTcpClose, this, _1);
        callback.onError = boost::bind(&ServerConnMgr::onServerTcpError, this, _1);
        callback.onTimeout = boost::bind(&ServerConnMgr::onServerTimeout, this, _1);
        m_server_tcp->SetNonBlock();
        m_server_tcp->SetCloseAfterExec();
        m_server_tcp->SetSockHandler(callback);
        m_server_tcp->SetTimeout(KEEP_ALIVE_TIME);
        EventLoopPool::AddTcpSockToLoop(NETMSGBUS_EVLOOP_NAME, m_server_tcp);
        return true;
    }
    void StopServerConnection()
    {
        UnRegisterNetMsgBusReceiver();
        //EventLoopPool::TerminateLoop("server_connect_loop");
        if(m_server_tcp)
            m_server_tcp->DisAllowSend();
        m_server_connecting = false;
        m_server_tcp.reset();
        g_log.Log(lv_debug, "stopping server connection.");
    }
    bool ProcessRspBody(uint32_t msg_id, kMsgBusBodyType body_type, const std::string& rsp_body)
    {
        RspBodyHandlerContainerT::const_iterator cit = m_allrsphandlers.find( body_type );
        if(cit != m_allrsphandlers.end())
        {
            (this->*(cit->second)) (msg_id, rsp_body);
            return true;
        }
        HandleUnknown(msg_id, rsp_body);
        return false;
    }
    void HandleRspConfirmAlive(uint32_t msg_id, const std::string& rsp_body)
    {
        MsgBusConfirmAliveRsp rsp;
        rsp.UnPackBody(rsp_body.data());
        if(rsp.ret_code == 0)
        {
        }
        else
        {
            g_log.Log(lv_warn, "confirm alive not confirmed.");
        }
    }
    void HandleRspRegister(uint32_t msg_id, const std::string& rsp_body)
    {
        // register has success.
        MsgBusRegisterRsp reg_rsp;
        reg_rsp.UnPackBody(rsp_body.data(), rsp_body.size());
        boost::shared_array<char>  rspdata(new char[MAX_SERVICE_NAME]);
        strncpy(rspdata.get(), reg_rsp.service_name, MAX_SERVICE_NAME);
        if(reg_rsp.ret_code == 0)
        {
            m_isreceiver_registered = true;
            m_receiver_name = std::string(reg_rsp.service_name);
            //PostMsg("netmsgbus.server.regreceiver.success", rspdata, MAX_SERVICE_NAME);
            LOG(g_log, lv_debug, "register service success. %s.", reg_rsp.service_name);
        }
        else
        {
            m_isreceiver_registered = false;
            //PostMsg("netmsgbus.server.regreceiver.failed", rspdata, MAX_SERVICE_NAME);
            LOG(g_log, lv_warn, "register service failed. %s.", reg_rsp.service_name);
            m_server_tcp->DisAllowSend();
        }
        boost::shared_ptr<NetFuture> ready_sendmsg_rsp = future_mgr_.safe_get_future(msg_id, true);
        //g_log.Log(lv_debug, "receiver future data returned to client:%lld, sid:%u, fd:%d\n", (int64_t)core::utility::GetTickCount(), future_sid, sp_tcp->GetFD());
        if(ready_sendmsg_rsp)
        {
            if (m_isreceiver_registered)
                ready_sendmsg_rsp->set_result(reg_rsp.service_name);
            else
                ready_sendmsg_rsp->set_error(reg_rsp.GetErrMsg());
        }
    }
    void HandleRspGetClient(uint32_t msg_id, const std::string& rsp_body)
    {
        MsgBusGetClientRsp rsp;
        rsp.UnPackBody(rsp_body.data());
        boost::shared_array<char> rspdata(new char[rsp_body.size()]);
        memcpy(rspdata.get(), rsp_body.data(), rsp_body.size());
        //PostMsg("netmsgbus.server.getclient", rspdata, rsp_body.size());
        if(rsp.ret_code == 0)
        {
            assert(rsp_body.size());
            //g_log.Log(lv_debug, "receiver future data returned to client:%lld, sid:%u, fd:%d\n", (int64_t)core::utility::GetTickCount(), future_sid, sp_tcp->GetFD());
        }
        else
        {
            LOG(g_log, lv_debug, "msgbus server return error while get client info, ret_code: %d.", rsp.ret_code);
        }
        boost::shared_ptr<NetFuture> ready_sendmsg_rsp = future_mgr_.safe_get_future(msg_id, true);
        if(ready_sendmsg_rsp)
        {
            ready_sendmsg_rsp->set_result(rsp_body);
        }
    }
    void HandleRspSendMsg(uint32_t msg_id, const std::string& rsp_body)
    {// 本客户端通过服务器向其他客户端转发消息得到的服务器返回确认
        MsgBusSendMsgRsp rsp;
        rsp.UnPackBody(rsp_body.data(), rsp_body.size());
        if(rsp.ret_code == 0)
        {
        }
        else
        {
            LOG(g_log, lv_debug, "send msg by server error: %d, errmsg: %s.", rsp.ret_code, std::string(rsp.GetErrMsg()).c_str());
        }
        boost::shared_ptr<NetFuture> ready_sendmsg_rsp = future_mgr_.safe_get_future(msg_id, true);
        if(ready_sendmsg_rsp)
        {
            if (rsp.ret_code != 0)
                ready_sendmsg_rsp->set_error(rsp.GetErrMsg());
            else
                ready_sendmsg_rsp->set_result("success");
        }
    }
    void HandleReqSendMsg(uint32_t msg_id, const std::string& rsp_body)
    {// 收到服务器转发的其他客户端的发消息请求
        MsgBusSendMsgReq req;
        req.UnPackBody(rsp_body.data(), rsp_body.size());
        if(!FilterMgr::FilterBySender(req.from_name))
        {
            LOG(g_log, lv_debug, "filter by sender: %s while got message from server relay.", req.from_name);
            return;
        }
        std::string msg_str(req.GetMsgContent(), req.msg_len);
        //LOG(g_log, lv_debug, "got message from server relay. from: %s, content:%s, msgid:%d", req.from_name, msg_str.c_str(), req.msg_id);
        NetMsgBusToLocalMsgBus(msg_str);
    }
    void HandleRspUnRegister(uint32_t msg_id, const std::string& rsp_body)
    {
        // unregister from msgbus server, receiver should shutdown, but we can still sendmsg.
        //s_server_connecting = false;
        m_server_tcp->DisAllowSend();
    }
    void HandleRspPBBody(uint32_t msg_id, const std::string& rsp_body)
    {
        //LOG(g_log, lv_debug, "query services response:%s", rsp_body.c_str());
        MsgBusPackPBType pbpack;

        pbpack.UnPackBody(rsp_body.data(), rsp_body.size());
        string pbtype(pbpack.GetPBType());
        string pbdata(pbpack.GetPBData(), pbpack.pbdata_len);
        PBHandlerContainerT::const_iterator cit = m_pb_handlers.find(pbtype);
        if(cit != m_pb_handlers.end())
        {
            cit->second->onPbData(msg_id, pbtype, pbdata);
        }
        else
        {
            LOG(g_log, lv_warn, "unknown pbtype:%s of protocol buffer data.", pbtype.c_str());
        }
    }

    void HandleQueryServicesRsp(uint32_t msg_id, PBQueryServicesRsp* pbrsp)
    {
        std::string service_name;

        google::protobuf::RepeatedPtrField<std::string>::const_iterator cit = pbrsp->service_name().begin();
        while(cit != pbrsp->service_name().end())
        {
            service_name += *cit + ",";
            ++cit;
        }
        LOG(g_log, lv_info, "msgid:%d, all available services:%s", msg_id, service_name.c_str());
        boost::shared_ptr<NetFuture> ready_sendmsg_rsp = future_mgr_.safe_get_future(msg_id, true);
        if(ready_sendmsg_rsp)
        {
            ready_sendmsg_rsp->set_result(service_name);
        }
    }

    void HandleUnknown(uint32_t msg_id, const std::string& rsp_body)
    {
        LOG(g_log, lv_warn, "receive a unknown rsp from msgbus server.");
    }

    // 注册本地的客户端接收消息的地址
    // 向网络服务器注册自己的消息接收客户端，告知消息总线以后使用此地址向该客户端进行消息的收发
    bool RegisterNetMsgBusReceiver(const std::string& clientip, unsigned short int clientport, 
        const std::string& clientname, kServerBusyState busy_state /* = LOW */)
    {
        m_receiver_ip = clientip;
        m_receiver_port = clientport;
        m_receiver_name = clientname;
        if(!m_server_connecting)
            return false;
        ClientHost host;
        if(clientip != "")
        {
            host.set_ip(clientip);
        }
        host.set_port(clientport);
        host.set_state(busy_state);

        std::pair<uint32_t, boost::shared_ptr<NetFuture> > future = future_mgr_.safe_insert_future();
        MsgBusRegisterReq reg_req;
        reg_req.msg_id = future.first;
        assert(clientname.size() < MAX_SERVICE_NAME);
        strncpy(reg_req.service_name, clientname.c_str(), MAX_SERVICE_NAME);
        reg_req.service_name[clientname.size()] = '\0';
        reg_req.service_host = host;
        boost::shared_array<char> outbuffer(new char[reg_req.Size()]);
        reg_req.PackData(outbuffer.get());

        assert(m_server_tcp);
        if(m_server_tcp)
        {
            bool ret = m_server_tcp->SendData(outbuffer.get(), reg_req.Size());
            if (!ret)
                return false;
            std::string rsp;
            ret = future.second->get(SERVER_CALL_TIMEOUT, rsp);
            if (!ret || future.second->has_err() || rsp.empty())
            {
                LOG(g_log, lv_warn, "register receiver failed. err:%s", rsp.c_str());
                return false;
            }
            return true;
        }
        else
            return false;
    }
    bool UnRegisterNetMsgBusReceiver()
    {
        if(!m_server_connecting)
            return false;
        MsgBusUnRegisterReq unreg_req;
        strncpy(unreg_req.service_name, m_receiver_name.c_str(), MAX_SERVICE_NAME);
        unreg_req.service_name[m_receiver_name.size()] = '\0';
        ClientHost host;
        if(m_receiver_ip != "")
        {
            host.set_ip(m_receiver_ip);
        }
        host.set_port(m_receiver_port);
        unreg_req.service_host = host;
        boost::shared_array<char> outbuffer(new char[unreg_req.Size()]);
        unreg_req.PackData(outbuffer.get());
        assert(m_server_tcp);
        if(m_server_tcp)
        {
            bool success = m_server_tcp->SendData(outbuffer.get(), unreg_req.Size());
            if(success)
                m_isreceiver_registered = false;
            return success;
        }
        return false;
    }
    // 更新某个已经注册过的消息接收客户端的工作状态，以便服务器根据负载状态动态选择
    // 如果没找到需要更新的客户端名字会返回错误
    bool UpdateReceiverBusyState(kServerBusyState busy_state)
    {
        if( !m_isreceiver_registered )
            return false;
        return RegisterNetMsgBusReceiver(m_receiver_ip, m_receiver_port, 
                m_receiver_name, busy_state);
    }

    // 使用服务器中转发送消息
    bool PostNetMsgUseServerRelay(const std::string& clientname, uint32_t data_len, boost::shared_array<char> data)
    {
        if(!m_server_connecting)
        {
            return false;
        }
        MsgBusSendMsgReq sendmsg_req;
        assert(clientname.size() < MAX_SERVICE_NAME);
        strncpy(sendmsg_req.dest_name, clientname.c_str(), MAX_SERVICE_NAME);
        sendmsg_req.dest_name[clientname.size()] = '\0';
        strncpy(sendmsg_req.from_name, m_receiver_name.c_str(), MAX_SERVICE_NAME);
        sendmsg_req.from_name[m_receiver_name.size()] = '\0';

        std::pair<uint32_t, boost::shared_ptr<NetFuture> > future = future_mgr_.safe_insert_future();
        sendmsg_req.msg_id = future.first;
        //g_log.Log(lv_debug, "server tick msgid %u, (tick %ld). sendmsg use server relay from:%s",
        //    sendmsg_req.msg_id, core::utility::GetTickCount(), m_receiver_name.c_str());
        sendmsg_req.msg_len = data_len;
        sendmsg_req.SetVarData( data.get() );
        boost::shared_array<char> req_data(new char[sendmsg_req.Size()]);
        sendmsg_req.PackData(req_data.get());
        assert(m_server_tcp);
        if(m_server_tcp)
        {
            bool ret = m_server_tcp->SendData(req_data.get(), sendmsg_req.Size());
            if (!ret)
                return false;
            std::string rsp;
            ret = future.second->get(SERVER_CALL_TIMEOUT, rsp);
            if (!ret || future.second->has_err())
            {
                LOG(g_log, lv_warn, "sendmsg use server relay failed. msgid:%u, err:%s.", future.first, rsp.c_str());
                return false;
            }
            return true;
        }
        return false;
    }
    bool ReqReceiverInfo(const std::string& clientname, NetFuture::futureCB cb)
    {
        if (cb == NULL)
            return false;
        std::string ip;
        unsigned short int port;
        return ReqReceiverInfo(clientname, ip, port, cb);
    }

    bool ReqReceiverInfo(const std::string& clientname, std::string& ip, unsigned short int& port)
    {
        return ReqReceiverInfo(clientname, ip, port, NULL);
    }

    bool QueryAvailableServices(const std::string& match_str, std::string& rsp)
    {
        if(!m_server_connecting)
        {
            LOG(g_log, lv_debug, "server not connecting while req receiver info.");
            return false;
        }
        //printf("request client name :%s\n", clientname.c_str());
        std::pair<uint32_t, boost::shared_ptr<NetFuture> > future = future_mgr_.safe_insert_future();
        MsgBusPackPBType services_query;
        services_query.msg_id = future.first;
        PBQueryServicesReq pbreq;
        pbreq.set_match_prefix(match_str);
        std::string pbtype = PBQueryServicesReq::descriptor()->full_name();
        services_query.pbtype_len = pbtype.size() + 1;
        pbtype.push_back('\0');
        int pbsize = pbreq.ByteSize();
        boost::shared_array<char> pbdata(new char[pbsize]);
        pbreq.SerializeToArray(pbdata.get(), pbsize);
        services_query.pbdata_len = pbsize;

        services_query.SetVarData(&pbtype[0], pbdata.get());
        
        boost::shared_array<char> req_data(new char[services_query.Size()]);
        services_query.PackData(req_data.get());
        assert(m_server_tcp);
        if(m_server_tcp)
        {
            bool ret = m_server_tcp->SendData(req_data.get(), services_query.Size());
            if (!ret)
                return false;
            ret = future.second->get(SERVER_CALL_TIMEOUT, rsp);
            if (!ret || future.second->has_err())
            {
                LOG(g_log, lv_warn, "query available services failed.msgid:%d, err:%s", future.first, rsp.c_str());
                return false;
            }
            return true;
        }
        return false;
    }

private:
    template<typename T> void regist_pbdata_handler(const typename PBHandlerT<T>::PBHandlerCB& cb)
    {
        boost::shared_ptr<PBHandlerT<T> > pbh(new PBHandlerT<T>(cb));
        m_pb_handlers[T::descriptor()->full_name()] = pbh;
    }
    bool ReqReceiverInfo(const std::string& clientname, std::string& ip, unsigned short int& port, NetFuture::futureCB cb)
    {
        if(!m_server_connecting)
        {
            LOG(g_log, lv_debug, "server not connecting while req receiver info.");
            return false;
        }
        std::pair<uint32_t, boost::shared_ptr<NetFuture> > future = future_mgr_.safe_insert_future(cb);
        MsgBusGetClientReq get_client_req;
        get_client_req.msg_id = future.first;

        assert(clientname.size() < MAX_SERVICE_NAME);
        strncpy(get_client_req.dest_name, clientname.c_str(), MAX_SERVICE_NAME);
        get_client_req.dest_name[clientname.size()] = '\0';
        boost::shared_array<char> req_data(new char[get_client_req.Size()]);
        get_client_req.PackData(req_data.get());
        assert(m_server_tcp);
        if(m_server_tcp)
        {
            bool ret = m_server_tcp->SendData(req_data.get(), get_client_req.Size());
            if (!ret)
                return false;
            if (cb == NULL)
            {
                std::string rspdata;
                ret = future.second->get(SERVER_CALL_TIMEOUT, rspdata);
                if (!ret || future.second->has_err() || rspdata.empty())
                {
                    LOG(g_log, lv_warn, "req receiver info failed.msgid:%d, err:%s", future.first, rspdata.c_str());
                    return false;
                }
                MsgBusGetClientRsp rsp;
                rsp.UnPackBody(rspdata.data(), rspdata.size());
                std::string clientname(rsp.dest_name);
                if(rsp.ret_code == 0)
                {
                    ip = rsp.dest_host.ip();
                    port = rsp.dest_host.port();
                    //LOG(g_log, lv_debug, "get client info returned. ret name : %s, ip:port : %s:%d", clientname.c_str(), ip.c_str(), port); 
                    return true;
                }
                LOG(g_log, lv_debug, "get client info returned error. ret name : %s, retcode: %d", clientname.c_str(), rsp.ret_code); 
                return false;
            }
            return true;
        }
        return false;
    }

    //EventLoopPool m_evpool;
    TcpSockSmartPtr m_server_tcp;
    std::string m_serverip;
    unsigned short int m_serverport;
    // 该服务器连接对应的消息总线接收者的信息，每个服务器连接对应唯一的一个接收者
    // 接收者使用该连接和消息总线服务器进行通信
    std::string m_receiver_ip;
    std::string m_receiver_name;
    unsigned short int m_receiver_port;
    volatile bool m_server_connecting;
    bool m_isreceiver_registered;
    typedef void (ServerConnMgr::*RspBodyHandlerFunc)(uint32_t msg_id, const std::string& rsp_body);
    typedef std::map< int, RspBodyHandlerFunc > RspBodyHandlerContainerT;
    RspBodyHandlerContainerT  m_allrsphandlers;
    const static int KEEP_ALIVE_TIME = 30000; //  keep tcp alive
    LoggerCategory g_log;
    PBHandlerContainerT m_pb_handlers;
    FutureMgr future_mgr_;
};
}
#endif// end of NETMSGBUS_SERVER_CONNMGR
