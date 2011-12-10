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

namespace NetMsgBus 
{
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
        //sp_tcp->SetTimeout(KEEP_ALIVE_TIME);
        return true;
    }

    size_t ServerRspProcess(TcpSockSmartPtr sp_tcp, const char* pdata, size_t size)
    {
        //if(size > 0)
        //{
        //    sp_tcp->SetTimeout(KEEP_ALIVE_TIME);
        //}
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
            ProcessRspBody(head.body_type, server_rsp_body);
            size -= needlen;
            readedlen += needlen;
            pdata += head.body_len;
        }
    }
    void onServerTcpClose(TcpSockSmartPtr sp_tcp)
    {
        m_server_connecting = true;
    }
    void onServerTcpError(TcpSockSmartPtr)
    {
        g_log.Log(lv_error, "disconnect from msgbus server for error.");
        m_server_connecting = true;
    }
    void onServerTimeout(TcpSockSmartPtr sp_tcp)
    {
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

#if defined (__APPLE__) || defined (__MACH__) 
        boost::shared_ptr<SockWaiterBase> spwaiter(new SelectWaiter());
#else
        boost::shared_ptr<SockWaiterBase> spwaiter(new EpollWaiter());
#endif
        m_server_tcp->SetNonBlock();
        m_server_tcp->SetCloseAfterExec();
        m_server_tcp->SetSockHandler(callback);
        m_server_tcp->SetTimeout(KEEP_ALIVE_TIME);
        spwaiter->AddTcpSock(m_server_tcp);
        m_evpool.CreateEventLoop("server_connect_loop", spwaiter);
        return true;
    }
    void StopServerConnection()
    {
        m_evpool.TerminateLoop("server_connect_loop");
        m_server_connecting = false;
    }
    bool ProcessRspBody(kMsgBusBodyType body_type, const std::string& rsp_body)
    {
        RspBodyHandlerContainerT::const_iterator cit = m_allrsphandlers.find( body_type );
        if(cit != m_allrsphandlers.end())
        {
            (this->*(cit->second)) (rsp_body);
            return true;
        }
        HandleUnknown(rsp_body);
        return false;
    }
    void HandleRspConfirmAlive(const std::string& rsp_body)
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
    void HandleRspRegister(const std::string& rsp_body)
    {
        // register has success.
        MsgBusRegisterRsp reg_rsp;
        assert((rsp_body.size() - sizeof(reg_rsp.err_msg_len) - sizeof(reg_rsp.ret_code) - MAX_SERVICE_NAME) > 0);
        boost::shared_array<char> msgbuf(new char[rsp_body.size() - sizeof(reg_rsp.err_msg_len) - sizeof(reg_rsp.ret_code) - MAX_SERVICE_NAME]);

        reg_rsp.err_msg = msgbuf.get();
        reg_rsp.UnPackBody(rsp_body.data());
        boost::shared_array<char>  rspdata(new char[MAX_SERVICE_NAME]);
        strncpy(rspdata.get(), reg_rsp.service_name, MAX_SERVICE_NAME);
        if(reg_rsp.ret_code == 0)
        {
            m_isreceiver_registered = true;
            m_receiver_name = std::string(reg_rsp.service_name);
            PostMsg("netmsgbus.server.regreceiver.success", rspdata, MAX_SERVICE_NAME);
        }
        else
        {
            m_isreceiver_registered = false;
            PostMsg("netmsgbus.server.regreceiver.failed", rspdata, MAX_SERVICE_NAME);
        }
    }
    void HandleRspGetClient(const std::string& rsp_body)
    {
        MsgBusGetClientRsp rsp;
        rsp.UnPackBody(rsp_body.data());
        boost::shared_array<char> rspdata(new char[rsp_body.size()]);
        memcpy(rspdata.get(), rsp_body.data(), rsp_body.size());
        PostMsg("netmsgbus.server.getclient", rspdata, rsp_body.size());
        if(rsp.ret_code == 0)
        {
            assert(rsp_body.size());
        }
        else
        {
            g_log.Log(lv_debug, "msgbus server return error while get client info, ret_code: %d.", rsp.ret_code);
        }
    }
    void HandleRspSendMsg(const std::string& rsp_body)
    {// 本客户端通过服务器向其他客户端转发消息得到的服务器返回确认
        MsgBusSendMsgRsp rsp;
        assert(rsp_body.size() - sizeof(rsp.ret_code) - sizeof(rsp.msg_id) - sizeof(rsp.err_msg_len));
        boost::shared_array<char> err_msg_buf(new char[rsp_body.size() - sizeof(rsp.ret_code) - sizeof(rsp.msg_id) - sizeof(rsp.err_msg_len)]);
        rsp.err_msg = err_msg_buf.get();
        rsp.UnPackBody(rsp_body.data());
        if(rsp.ret_code == 0)
        {
        }
        else
        {
            g_log.Log(lv_debug, "send msg by server error: %d, errmsg: %s.", rsp.ret_code, std::string(rsp.err_msg).c_str());
        }
    }
    void HandleReqSendMsg(const std::string& rsp_body)
    {// 收到服务器转发的其他客户端的发消息请求
        MsgBusSendMsgReq req;
        assert(rsp_body.size() - sizeof(req.msg_id) - sizeof(req.msg_len) - MAX_SERVICE_NAME*2);
        boost::shared_array<char> msgbuf(new char[rsp_body.size() - sizeof(req.msg_id) - sizeof(req.msg_len) - MAX_SERVICE_NAME*2]);
        req.msg_content = msgbuf.get();
        req.UnPackBody(rsp_body.data());
        if(!FilterMgr::FilterBySender(req.from_name))
        {
            g_log.Log(lv_debug, "filter by sender: %s while got message from server relay.", req.from_name);
            return;
        }
        std::string msg_str(req.msg_content, req.msg_len);
        NetMsgBusToLocalMsgBus(msg_str);
    }
    void HandleRspUnRegister(const std::string& rsp_body)
    {
        // unregister from msgbus server, receiver should shutdown, but we can still sendmsg.
        //s_server_connecting = false;
    }
    void HandleUnknown(const std::string& rsp_body)
    {
        g_log.Log(lv_warn, "receive a unknown rsp from msgbus server.");
    }

    // 注册本地的客户端接收消息的地址
    // 向网络服务器注册自己的消息接收客户端，告知消息总线以后使用此地址向该客户端进行消息的收发
    bool RegisterNetMsgBusReceiver(const std::string& clientip, unsigned short int clientport, 
        const std::string& clientname, kServerBusyState busy_state /* = LOW */)
    {
        if(!m_server_connecting)
            return false;
        m_receiver_ip = clientip;
        m_receiver_port = clientport;
        m_receiver_name = clientname;
        ClientHost host;
        host.server_ip = 0;
        if(clientip != "")
        {
            inet_pton(AF_INET, clientip.c_str(), &host.server_ip);
            //host.server_ip = inet_addr(clientip.c_str());
        }
        host.server_port = htons(clientport);
        host.busy_state = busy_state;

        MsgBusRegisterReq reg_req;
        assert(clientname.size() < MAX_SERVICE_NAME);
        strncpy(reg_req.service_name, clientname.c_str(), MAX_SERVICE_NAME);
        reg_req.service_name[clientname.size()] = '\0';
        reg_req.service_host = host;
        boost::shared_array<char> outbuffer(new char[reg_req.Size()]);
        reg_req.PackData(outbuffer.get());

        assert(m_server_tcp);
        if(m_server_tcp)
            return m_server_tcp->SendData(outbuffer.get(), reg_req.Size());
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
        host.server_ip = 0;
        if(m_receiver_ip != "")
        {
            inet_pton(AF_INET, m_receiver_ip.c_str(), &host.server_ip);
            //host.server_ip = inet_addr(m_receiver_ip.c_str());
        }
        host.server_port = htons(m_receiver_port);
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

        sendmsg_req.msg_id = (uint32_t)core::utility::GetTickCount();
        //printf("server tick msgid %u, (tick %ld). sendmsg use server relay from:%s\n",
        //    sendmsg_req.msg_id, core::utility::GetTickCount(), m_receiver_name.c_str());
        sendmsg_req.msg_len = data_len;
        sendmsg_req.msg_content = data.get();
        boost::shared_array<char> req_data(new char[sendmsg_req.Size()]);
        sendmsg_req.PackData(req_data.get());
        assert(m_server_tcp);
        if(m_server_tcp)
        {
            return m_server_tcp->SendData(req_data.get(), sendmsg_req.Size());
        }
        return false;
    }
    bool ReqReceiverInfo(const std::string& clientname)
    {
        if(!m_server_connecting)
        {
            g_log.Log(lv_debug, "server not connecting while req receiver info.");
            return false;
        }
        //printf("request client name :%s\n", clientname.c_str());
        MsgBusGetClientReq get_client_req;
        assert(clientname.size() < MAX_SERVICE_NAME);
        strncpy(get_client_req.dest_name, clientname.c_str(), MAX_SERVICE_NAME);
        get_client_req.dest_name[clientname.size()] = '\0';
        boost::shared_array<char> req_data(new char[get_client_req.Size()]);
        get_client_req.PackData(req_data.get());
        assert(m_server_tcp);
        if(m_server_tcp)
            return m_server_tcp->SendData(req_data.get(), get_client_req.Size());
        return false;
    }
private:
    EventLoopPool m_evpool;
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
    typedef void (ServerConnMgr::*RspBodyHandlerFunc)(const std::string& rsp_body);
    typedef std::map< int, RspBodyHandlerFunc > RspBodyHandlerContainerT;
    RspBodyHandlerContainerT  m_allrsphandlers;
    const static int KEEP_ALIVE_TIME = 30000; // server is 120000, here we half it to keep tcp alive
    LoggerCategory g_log;
};
}
#endif// end of NETMSGBUS_SERVER_CONNMGR
