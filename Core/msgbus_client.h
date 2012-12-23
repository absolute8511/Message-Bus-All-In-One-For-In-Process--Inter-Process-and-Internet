#ifndef MSGBUS_CLIENT_H
#define MSGBUS_CLIENT_H
#include "msgbus_def.h"
#include "NetMsgBusFuture.hpp"
#include <boost/shared_array.hpp>
#include <string>

namespace NetMsgBus
{
class NetFuture;
// 将本地消息接收客户端注册到网络消息总线服务器上, ip使用空串则使用本地ip
bool msgbus_register_client_receiver(const std::string& clientip, unsigned short int& clientport,
   const std::string& clientname, kServerBusyState busy_state = LOW);
// 更新本地消息接收者的负载状态以便服务器动态选择客户端
bool msgbus_update_receiver_busystate(const std::string& clientname, kServerBusyState busy_state);
// 向服务器请求某个命名的消息接收者主机信息，然后缓存此客户端信息
bool msgbus_req_receiver_info(const std::string& clientname);
// 通过服务器中转发送消息，支持以前缀的方式匹配一组客户端名称，然后进行组广播消息
bool msgbus_postmsg_use_server_relay(const std::string& clientname, uint32_t data_len, boost::shared_array<char> data);
// post message to all registed client in the netmsgbus.
bool msgbus_postmsg_broadcast(uint32_t data_len, boost::shared_array<char> data);
// 直接向客户端发送消息，客户端名称使用精确匹配方式
boost::shared_ptr<NetFuture> msgbus_postmsg_direct_to_client(const std::string& clientname, uint32_t data_len,
    boost::shared_array<char> data, NetFuture::futureCB callback = NULL);
boost::shared_ptr<NetFuture> msgbus_postmsg_direct_to_client(const std::string& dest_ip, unsigned short dest_port,
    uint32_t data_len, boost::shared_array<char> data, NetFuture::futureCB callback = NULL);
// 同步发送消息，由于是同步的，因此此函数不会像服务器请求新的客户端信息，而是使用缓存中的数据
// 如果缓存数据不存在或者已失效，那么该函数返回失败。
// 为了提高发送成功率，应该先使用异步方式从服务器获取该客户端信息，成功后再调用该函数
// 本函数默认超时时间为30s,返回的数据在rsp_content 中
bool msgbus_sendmsg_direct_to_client(const std::string& clientname, uint32_t data_len, 
    boost::shared_array<char> data, std::string& rsp_content, int32_t timeout_sec = 30);
bool msgbus_sendmsg_direct_to_client(const std::string& dest_ip, unsigned short dest_port, uint32_t data_len, 
   boost::shared_array<char> data, std::string& rsp_content, int32_t timeout_sec = 30);
//void msgbus_disconnect_receiver(const std::string& name);

bool msgbus_query_available_services(const std::string& match_str);

bool init_netmsgbus_client(const std::string& serverip, unsigned short int serverport);
void destroy_netmsgbus_client();
void disconnect_from_server();

}

#endif
