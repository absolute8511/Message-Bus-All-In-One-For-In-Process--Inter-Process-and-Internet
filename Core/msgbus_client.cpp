#include "msgbus_client.h"
#include "NetMsgBusServerConnMgr.hpp"
#include "NetMsgBusReceiverMgr.hpp"
#include "NetMsgBusReq2ReceiverMgr.hpp"
#include "MsgHandlerMgr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <boost/shared_array.hpp>

using namespace std;
namespace NetMsgBus
{

static ServerConnMgr   s_server_connmgr;
static ReceiverMgr     s_receiver_mgr;
static Req2ReceiverMgrPtr sp_req2receiver_mgr;

// 以同步方式发送数据请求到客户端，并将客户端返回的数据保存在指定变量中。
bool msgbus_sendmsg_direct_to_client(const std::string& clientname, uint32_t data_len, 
   boost::shared_array<char> data, string& rsp_content, int32_t timeout_sec)
{
    if(clientname == "")
        return false;
    if(!sp_req2receiver_mgr)
        return false;
    return sp_req2receiver_mgr->SendMsgDirectToClient(clientname, data_len, data, rsp_content, timeout_sec);
}
//
bool msgbus_sendmsg_direct_to_client(const std::string& dest_ip, unsigned short dest_port, uint32_t data_len, 
   boost::shared_array<char> data, string& rsp_content, int32_t timeout_sec)
{
    if(dest_ip == "")
        return false;
    if(!sp_req2receiver_mgr)
        return false;
    return sp_req2receiver_mgr->SendMsgDirectToClient(dest_ip, dest_port, data_len, data, rsp_content, timeout_sec);
}

// 注册本地的客户端接收消息的地址
// 向网络服务器注册自己的消息接收客户端，告知消息总线以后使用此地址向该客户端进行消息的收发
bool msgbus_register_client_receiver(const std::string& clientip, unsigned short int& clientport, 
    const std::string& clientname, kServerBusyState busy_state /* = LOW */)
{
    // if need listen local the first start receiver at local port. then register to server.
    // if no local listen then just for connect identify
    if((clientport != 0) && !s_receiver_mgr.StartReceiver(clientport)) 
    {
        return false;
    }
    return s_server_connmgr.RegisterNetMsgBusReceiver(clientip, clientport, clientname, busy_state);
}

bool msgbus_unregister_client_receiver()
{
    return s_server_connmgr.UnRegisterNetMsgBusReceiver();
}

// 更新某个已经注册过的消息接收客户端的工作状态，以便服务器根据负载状态动态选择
// 如果没找到需要更新的客户端名字会返回错误
bool msgbus_update_receiver_busystate(kServerBusyState busy_state)
{
    return s_server_connmgr.UpdateReceiverBusyState(busy_state);
}

// 使用服务器中转发送消息
bool msgbus_postmsg_use_server_relay(const std::string& clientname, uint32_t data_len, boost::shared_array<char> data)
{
    return s_server_connmgr.PostNetMsgUseServerRelay(clientname, data_len, data);
}

bool msgbus_postmsg_broadcast(uint32_t data_len, boost::shared_array<char> data)
{
    return msgbus_postmsg_use_server_relay("", data_len, data);
}

// 直接使用缓存的客户端信息发送消息，如果缓存不存在会获取服务器上的数据后再重试
boost::shared_ptr<NetFuture> msgbus_postmsg_direct_to_client(const std::string& clientname, uint32_t data_len, boost::shared_array<char> data)
{
    if(!sp_req2receiver_mgr)
        return boost::shared_ptr<NetFuture>();
    return sp_req2receiver_mgr->PostMsgDirectToClient(clientname, data_len, data);
}
//
boost::shared_ptr<NetFuture> msgbus_postmsg_direct_to_client(const std::string& dest_ip, unsigned short dest_port,
    uint32_t data_len, boost::shared_array<char> data)
{
    if(dest_ip.empty() || !sp_req2receiver_mgr)
        return boost::shared_ptr<NetFuture>();
    return sp_req2receiver_mgr->PostMsgDirectToClient(dest_ip, dest_port, data_len, data);
}

bool msgbus_req_receiver_info(const std::string& clientname)
{
    return s_server_connmgr.ReqReceiverInfo(clientname);
}

bool msgbus_query_available_services(const std::string& match_str)
{
    return s_server_connmgr.QueryAvailableServices(match_str);
}

//void msgbus_disconnect_receiver(const std::string& name)
//{
//    if(sp_req2receiver_mgr)
//        sp_req2receiver_mgr->DisConnectFromClient(name);
//}

bool init_netmsgbus_client(const std::string& serverip, unsigned short int serverport)
{
    if(!s_server_connmgr.StartServerCommunicateLoop(serverip, serverport))
    {
        // load local static client info.
        printf("net msgbus server init failed, using non-center-server mode.\n");
        //return false;
    }
    
    MsgHandlerMgr::GetInstance(sp_req2receiver_mgr, false);
    if(!sp_req2receiver_mgr)
        return false;
    sp_req2receiver_mgr->SetServerConnMgr(&s_server_connmgr);
    if(!sp_req2receiver_mgr->Start())
        return false;

    return true;
}
void destroy_netmsgbus_client()
{
    msgbus_unregister_client_receiver();
    s_receiver_mgr.StopReceiver();
    if(sp_req2receiver_mgr)
        sp_req2receiver_mgr->Stop();
    s_server_connmgr.StopServerConnection();
    if(sp_req2receiver_mgr)
        sp_req2receiver_mgr->SetServerConnMgr(NULL);
}

} // end of namespace NetMsgBus
