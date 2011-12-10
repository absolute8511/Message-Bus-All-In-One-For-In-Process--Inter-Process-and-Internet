// 消息总线的注册服务，每个进程注册自己关注的消息类型，并将该消息类型关联到
// 自己的主机IP相应的端口上，注册成功后，所有具有该消息类型的消息都会发到
// 关联过的IP+PORT上，消息服务主机在收到消息后放入到自己的消息总线，然后进行
// 进程内的消息分发
#include "msgbus_def.h"
#include "condition.hpp"
#include "lock.hpp"

#if defined (__APPLE__) || defined (__MACH__) 
#include "SelectWaiter.h"
#else
#include "EpollWaiter.h"
#endif

#include "EventLoopPool.h"
#include "CommonUtility.hpp"
#include "threadpool.h"
#include "SimpleLogger.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <map>
#include <vector>
#include <deque>
#include <string>
#include <pthread.h>
#include <algorithm>
#include <boost/shared_array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <fcntl.h>
#include <iostream>
#include <limits.h>

#define MSGBUS_SERVER_DEFAULT_PORT 19000
#define TIMEOUT_SHORT 2
#define KEEP_ALIVE_TIME  90000

using namespace std;
using namespace NetMsgBus;
using namespace core::net;
using namespace core;

static LoggerCategory g_log("msgbus_server");
// todo: write a hashmap to store many host servers
//

typedef vector<ClientHost> ClientHostContainer;
// 这个容器相当于一个域名解析服务，存储对应服务名字的所有主机ip和端口
typedef map< string, ClientHostContainer > ServiceContainer;
static ServiceContainer available_services;
// 这个容器是和服务器有活动的tcp连接的所有具备服务的主机。
typedef map< string, TcpSockContainerT > ActiveClientTcpContainer;
static ActiveClientTcpContainer active_clients;
core::common::locker g_activeclients_locker;
static volatile bool s_netmsgbus_server_running = false;
static volatile bool s_netmsgbus_server_terminate = false;
static unsigned short s_server_port;

struct ReqTask{
    std::string client_name;
    uint32_t data_len;
    boost::shared_array<char> data;
};
struct BroadcastTask
{
    uint32_t data_len;
    boost::shared_array<char> data;
};
std::deque< ReqTask > reqtoclient_task_container;
std::deque< BroadcastTask > broadcast_task_container;
core::common::condition g_reqtask_cond;
core::common::locker g_reqtask_locker;

void process_data_from_client(TcpSockSmartPtr sp_tcp, const MsgBusPackHead& head, boost::shared_array<char> bodybuffer);
void process_register_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len);
void process_unregister_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len);
void process_sendmsg_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len);
void process_getclient_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len);
void process_confirm_alive_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len);

size_t server_onRead(TcpSockSmartPtr sp_tcp, const char* pdata, size_t size);
bool server_onSend(TcpSockSmartPtr sp_tcp);
void server_onError(TcpSockSmartPtr sp_tcp);
void server_onClose(TcpSockSmartPtr sp_tcp);
void server_onTimeout(TcpSockSmartPtr sp_tcp);

bool msgbus_queue_reqtoclient(const ReqTask& req_task)
{
    core::common::locker_guard guard(g_reqtask_locker);
    reqtoclient_task_container.push_back(req_task);
    g_reqtask_cond.notify_one();
    return true;
}
bool msgbus_queue_broadcast(const BroadcastTask& broadcast_task)
{
    core::common::locker_guard guard(g_reqtask_locker);
    broadcast_task_container.push_back(broadcast_task);
    g_reqtask_cond.notify_one();
    return true;
}

bool is_prefix_matching(const std::string& left, const std::string& right)
{
    return left.size() > right.size() ? left.substr(0, right.size()) == right : right.substr(0, left.size()) == left;
}

// give a method to select the best client to server to realize load balancing. 
template<typename TContainer, typename TElem> 
bool msgbus_select_best_client(const TContainer& container, TElem& bestelem)
{
    if ( container.size() > 0 )
    {
        typename TContainer::const_iterator pos = container.begin();
        bestelem = *pos;
        int randselect = (int)((rand()/(RAND_MAX + 1.0))*(container.size()));
        /*while (pos != it->second.end())
          {
          if( randselect-- < 0)
          {
          host = *pos;
          break;
          }
          ++pos;
          }*/
        bestelem = *(pos + randselect);
        return true;
    }
    else
    {
        return false;
    }
}

// 接收客户端请求的处理线程
void* msgbus_server_accept_thread( void* param )
{
    int server_sockfd;
    int client_sockfd;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    int result;
    fd_set readfds;

    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // set SO_REUSEADDR in order to restart quickly after crash
    int optval = 1;
    setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(s_server_port);
    int server_addr_len = sizeof(server_address);
    int ret = bind(server_sockfd, (struct sockaddr *)&server_address, server_addr_len);
    if(ret == -1)
    {
        g_log.Log(lv_warn, "msgbus server bind port:%d error.", s_server_port);
        s_netmsgbus_server_terminate = true;
        s_netmsgbus_server_running = false;
        return 0;
    }
    ret = listen(server_sockfd,5);
    if(ret == -1)
    {
        g_log.Log(lv_error, "listen socket error.");
        s_netmsgbus_server_terminate = true;
        s_netmsgbus_server_running = false;
        return 0;
    }

    FD_ZERO(&readfds);
    FD_SET(server_sockfd, &readfds);

    int max_sockfd = server_sockfd;
    available_services.clear();
    active_clients.clear();

#if defined (__APPLE__) || defined (__MACH__) 
    boost::shared_ptr< SockWaiterBase > spwaiter(new SelectWaiter());
#else
    boost::shared_ptr< SockWaiterBase > spwaiter(new EpollWaiter());
#endif
    EventLoopPool evpool;
    evpool.CreateEventLoop("server_accept_loop", spwaiter);
    SockHandler tcpcb;
    tcpcb.onRead = boost::bind(server_onRead, _1, _2, _3);
    tcpcb.onSend = boost::bind(server_onSend, _1);
    tcpcb.onError = boost::bind(server_onError, _1);
    tcpcb.onClose = boost::bind(server_onClose, _1);
    tcpcb.onTimeout = boost::bind(server_onTimeout, _1);

    if( !core::utility::set_fd_close_onexec(server_sockfd))
    {
        g_log.Log(lv_error, "set fd close on exec error.");
        s_netmsgbus_server_running = false;
        return 0;
    }
    if( !core::utility::set_fd_nonblock(server_sockfd))
    {
        g_log.Log(lv_error, "set fd nonblock error.");
        s_netmsgbus_server_running = false;
        return 0;
    }

    g_log.Log(lv_warn, "server waiting on port: %d ...", s_server_port);
    s_netmsgbus_server_running = true;
    // 长时间没有客户端连接的话,自动退出服务端
    int noclient_quit_cnt = 0;
    while(1){
        fd_set testreadfds = readfds;

        struct timeval tv;
        tv.tv_sec = TIMEOUT_SHORT;
        tv.tv_usec = 0;
        result = select(max_sockfd + 1, &testreadfds, (fd_set *)0, 0, &tv);
        if(result == -1) {
            g_log.Log(lv_error, "server select error.");
            continue;
        }
        if(active_clients.empty())
        {
            ++noclient_quit_cnt;
#ifdef NDEBUG
            if(noclient_quit_cnt > 2)
#else
            if(noclient_quit_cnt > 15)
#endif
            {
                s_netmsgbus_server_terminate = true;
            }
        }
        else
        {
            noclient_quit_cnt = 0;
        }
        if(s_netmsgbus_server_terminate)
        {
            break;
        }
        if(result == 0)
        {
            // timeout
            continue;
        }
        if(FD_ISSET(server_sockfd, &testreadfds)) {
            int client_len = sizeof(client_address);
            client_sockfd = accept(server_sockfd, (struct sockaddr*)&client_address, (socklen_t *)&client_len);
            if(client_sockfd >= 0)
            {
                char ipstr[INET_ADDRSTRLEN];
                TcpSockSmartPtr sp_tcp(new TcpSock(client_sockfd, 
                        inet_ntop(AF_INET, &client_address.sin_addr, ipstr, sizeof(ipstr)), ntohs(client_address.sin_port)));
                sp_tcp->SetNonBlock();
                sp_tcp->SetCloseAfterExec();
                sp_tcp->SetSockHandler(tcpcb);
                sp_tcp->SetTimeout(KEEP_ALIVE_TIME);
                spwaiter->AddTcpSock(sp_tcp);
                std::string ip;
                unsigned short int port;
                sp_tcp->GetDestHost(ip, port);
                g_log.Log(lv_debug, "a new client connected fd:%d, ip:port = %s:%d.",
                    client_sockfd, ip.c_str(), port);
            }
        }
    }
    close(server_sockfd);
    evpool.TerminateLoop("server_accept_loop");
    s_netmsgbus_server_running = false;
    return 0;
}

size_t server_onRead(TcpSockSmartPtr sp_tcp, const char* pdata, size_t size)
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
        if( size < needlen )
            return readedlen;
        MsgBusPackHead head;
        if (-1 == head.UnPackHead(pdata))
        {
            g_log.Log(lv_warn, "unpack head error.");
            return readedlen;
        }
        pdata += head.Size();
        needlen += head.body_len;
        if( size < needlen )
            return readedlen;
        assert(head.body_len);
        boost::shared_array<char> bodybuffer(new char[head.body_len]);
        memcpy(bodybuffer.get(), pdata, head.body_len);
        readedlen += needlen;
        size -= needlen;
        pdata += head.body_len;

        //g_log.Log(lv_debug, "receive head , msg_type:%u, body_type:%#x.",head.msg_type, head.body_type);
        threadpool::queue_work_task(boost::bind(process_data_from_client, sp_tcp, head, bodybuffer), 0);
        //process_data_from_client(sp_tcp, head, bodybuffer);
    }
}

void process_data_from_client(TcpSockSmartPtr sp_tcp, const MsgBusPackHead& head, boost::shared_array<char> bodybuffer)
{

    //g_log.Log(lv_debug, "server process data from client in thread : %lu.", (unsigned long)pthread_self());
    if( head.msg_type == 0 )
    {
        // request msg
        switch(head.body_type)
        {
        case REQ_REGISTER:
            {
                process_register_req(sp_tcp, bodybuffer, head.body_len);
            }
            break;
        case REQ_GETCLIENT:
            {
                process_getclient_req(sp_tcp, bodybuffer, head.body_len);
            }
            break;
        case REQ_SENDMSG:
            {
                process_sendmsg_req(sp_tcp, bodybuffer, head.body_len);
            }
            break;
        case REQ_UNREGISTER:
            {
                process_unregister_req(sp_tcp, bodybuffer, head.body_len);
            }
            break;
        case REQ_CONFIRM_ALIVE:
            {
                process_confirm_alive_req(sp_tcp, bodybuffer, head.body_len);
            }
            break;
        default:
            break;
        }
    }
    else if( head.msg_type == 1 )
    {
        // response from client
    }
    else
    {
        g_log.Log(lv_warn, "unknown data from client.");
    }
}

bool server_onSend(TcpSockSmartPtr sp_tcp)
{
    //sp_tcp->SetTimeout(KEEP_ALIVE_TIME);
    return true;
}

void server_onTimeout(TcpSockSmartPtr sp_tcp)
{
    server_onClose(sp_tcp);
    sp_tcp->Close();
}

void server_onError(TcpSockSmartPtr sp_tcp)
{
    // error, remove the client.
    server_onClose(sp_tcp);
}

void server_onClose(TcpSockSmartPtr sp_tcp)
{
    g_log.Log(lv_debug, "removing client fd: %d in server.", sp_tcp->GetFD());
    // remove the client info from the map.
    core::common::locker_guard guard(g_activeclients_locker);
    ActiveClientTcpContainer::iterator it = active_clients.begin();
    while( it != active_clients.end() )
    {
        TcpSockContainerT::iterator clientit = std::find_if(it->second.begin(), it->second.end(), IsSameTcpSock( sp_tcp ));
        if( clientit != it->second.end() )
        {
            it->second.erase(clientit);
            if(it->second.empty())
                active_clients.erase(it);
            break;
        }
        ++it;
    }
}

void process_confirm_alive_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len)
{
    MsgBusConfirmAliveReq alive_req;
    alive_req.UnPackBody(bodybuffer.get());
    if(alive_req.alive_flag != 0)
    {
        g_log.Log(lv_debug, "keep alive flag is wrong. flag:%d .", alive_req.alive_flag);
    }
    MsgBusConfirmAliveRsp rsp;
    rsp.ret_code = alive_req.alive_flag;
    boost::shared_array<char> buf(new char[rsp.Size()]);
    rsp.PackData(buf.get());
    if(sp_tcp)
        sp_tcp->SendData(buf.get(), rsp.Size());
}

void process_register_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len)
{
    MsgBusRegisterReq reg_req;
    reg_req.UnPackBody(bodybuffer.get());
    string service_name(reg_req.service_name);

    MsgBusRegisterRsp rsp;
    rsp.ret_code = 0;
    rsp.err_msg_len = 1;
    strncpy(rsp.service_name, reg_req.service_name, MAX_SERVICE_NAME);
    string errmsg;

    if(service_name == "")
    {
        rsp.ret_code = 1;
        errmsg = string("empty name is not allowed.");
        rsp.err_msg_len = errmsg.size() + 1;
    }
    else
    {
        ClientHost host = reg_req.service_host;
        if(host.server_ip == 0)
        {
            if(sp_tcp)
            {
                std::string ip;
                unsigned short int port;
                sp_tcp->GetDestHost(ip, port);
                inet_pton(AF_INET, ip.c_str(), &host.server_ip);
            }
            else
            {
                rsp.ret_code = 1;
                errmsg = string("can not get host ip for receiver service.");
                rsp.err_msg_len = errmsg.size() + 1;
            }
        }
        g_log.Log(lv_debug, "receive register request, name:%s.", service_name.c_str());
        core::common::locker_guard guard(g_activeclients_locker);
        ServiceContainer::iterator it = available_services.find(service_name);
        if (it != available_services.end())
        {
            ClientHostContainer::iterator pos = std::find_first_of(it->second.begin(), 
                it->second.end(), &host, &host + 1, ClientHostIsEqual());
            if (pos != it->second.end())
            {
                // update the server host state
                pos->busy_state = host.busy_state;
                g_log.Log(lv_debug, "update the server host state.new state %d.", host.busy_state);
            }
            else
            {
                it->second.push_back(host);
                // 存储当前服务对应的活动连接，以便其他地方直接拿到该连接符来发送数据
                active_clients[service_name].push_back(sp_tcp);
                g_log.Log(lv_debug, "a new host added to an exist service.");
                g_log.Log(lv_debug, "new add server host is %s:%d.", 
                    inet_ntoa(*((in_addr*)&host.server_ip)), ntohs(host.server_port));
            }
        }
        else
        {
            ClientHostContainer container;
            container.push_back(host);
            available_services[service_name] = container;
            // 存储当前服务对应的活动连接，以便其他地方直接拿到该连接符来发送数据
            active_clients[service_name].push_back(sp_tcp);
            g_log.Log(lv_debug, "new register service, server host is %s:%d.",
                inet_ntoa(*((in_addr*)&host.server_ip)), ntohs(host.server_port));
        }
    }
    assert(rsp.err_msg_len);
    boost::shared_array<char> errmsgbuf(new char[rsp.err_msg_len]);
    rsp.err_msg = errmsgbuf.get();
    strncpy(rsp.err_msg, errmsg.c_str(), rsp.err_msg_len);
    rsp.err_msg[rsp.err_msg_len - 1] = '\0';

    boost::shared_array<char> outbuffer( new char[rsp.Size()] );
    rsp.PackData(outbuffer.get());

    if( sp_tcp )
        sp_tcp->SendData(outbuffer.get(), rsp.Size());
}

void process_unregister_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len)
{
    MsgBusUnRegisterReq unreg_req;
    unreg_req.UnPackBody(bodybuffer.get());
    string service_name(unreg_req.service_name);

    if(service_name == "")
    {
        return;
    }
    else
    {
        ClientHost host = unreg_req.service_host;
        g_log.Log(lv_debug, "receive unregister request, name:%s.", service_name.c_str());
        if(host.server_ip == 0)
        {
            if(sp_tcp)
            {
                std::string ip;
                unsigned short int port;
                sp_tcp->GetDestHost(ip, port);
                //host.server_ip = inet_addr(ip.c_str());
                inet_pton(AF_INET, ip.c_str(), &host.server_ip);
            }
            else
            {
                return;
            }
        }
        core::common::locker_guard guard(g_activeclients_locker);
        ServiceContainer::iterator it = available_services.find(service_name);
        if (it != available_services.end())
        {
            ClientHostContainer::iterator pos = std::find_first_of(it->second.begin(), 
                it->second.end(), &host, &host + 1, ClientHostIsEqual());
            if (pos != it->second.end())
            {
                // update the server host state
                it->second.erase(pos);
                g_log.Log(lv_debug,"unregister server host is %s:%d", 
                    inet_ntoa(*((in_addr*)&host.server_ip)), ntohs(host.server_port));
            }
        }
    }
}

void process_sendmsg_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len)
{
    MsgBusSendMsgReq req;
    assert(body_len - sizeof(req.msg_id) - sizeof(req.msg_len) - MAX_SERVICE_NAME*2);
    boost::shared_array<char> msg_content_buf(new char[body_len - sizeof(req.msg_id) - sizeof(req.msg_len) - MAX_SERVICE_NAME*2]);
    req.msg_content = msg_content_buf.get();
    req.UnPackBody(bodybuffer.get());
    string dest_name(req.dest_name);
    g_log.Log(lv_debug, "server relay sendmsg from client:%s, content:%s. dest:%s", 
        string(req.from_name).c_str(), req.msg_content, dest_name.c_str());
    // support the prefix matching of client_name , so we can send messages to group of clients.
    bool is_exist = false;
    {
        core::common::locker_guard guard(g_activeclients_locker);
        ActiveClientTcpContainer::const_iterator cit = active_clients.begin();
        while(cit != active_clients.end())
        {
            if(is_prefix_matching(cit->first, dest_name))
            {
                if(cit->second.size() > 0)
                {
                    is_exist = true;
                    break;
                }
            }
            ++cit;
        }
    }
    MsgBusSendMsgRsp rsp;
    rsp.msg_id = req.msg_id;
    rsp.ret_code = 0;
    rsp.err_msg_len = 2;

    std::string errmsg;
    if(is_exist)
    {
        // forward the data to other client.
        boost::shared_array<char> outbuffer(new char[req.Size()]);
        req.PackData(outbuffer.get());

        // 转发数据
        if(dest_name == "")
        {
            BroadcastTask btask;
            btask.data_len = req.Size();
            btask.data = outbuffer;
            msgbus_queue_broadcast(btask);
        }
        else
        {
            ReqTask ctask;
            ctask.client_name = dest_name;
            ctask.data_len = req.Size();
            ctask.data = outbuffer;
            msgbus_queue_reqtoclient(ctask);
        }

    }
    else
    {
        rsp.ret_code = 1;
        errmsg = "dest client not found.";
        rsp.err_msg_len = errmsg.size() + 1;
    }

    boost::shared_array<char> err_buf(new char[rsp.err_msg_len]);
    rsp.err_msg = err_buf.get();
    strncpy(rsp.err_msg, errmsg.c_str(), rsp.err_msg_len);
    rsp.err_msg[rsp.err_msg_len - 1] = '\0';

    boost::shared_array<char> rspbuffer(new char[rsp.Size()]);
    rsp.PackData(rspbuffer.get());
    if(sp_tcp)
        sp_tcp->SendData(rspbuffer.get(), rsp.Size());
}

void process_getclient_req(TcpSockSmartPtr sp_tcp, boost::shared_array<char> bodybuffer, uint32_t body_len)
{
    MsgBusGetClientReq req;
    req.UnPackBody(bodybuffer.get());
    string dest_name(req.dest_name);
    g_log.Log(lv_debug, "receive get client info request, client name:%s.", dest_name.c_str());

    MsgBusGetClientRsp rsp;
    strncpy(rsp.dest_name, req.dest_name, MAX_SERVICE_NAME);
    g_log.Log(lv_debug, "rsp client name:%s.", rsp.dest_name);

    ClientHost host;
    core::common::locker_guard guard(g_activeclients_locker);
    ServiceContainer::const_iterator cit = available_services.find(dest_name);
    if(cit != available_services.end() && msgbus_select_best_client(cit->second, host))
    {
        rsp.ret_code = 0;
        rsp.dest_host = host;
    }
    else
    {
        rsp.ret_code = 1;
    }
    boost::shared_array<char> outbuffer( new char[rsp.Size()] );
    rsp.PackData(outbuffer.get());
    if(sp_tcp)
        sp_tcp->SendData(outbuffer.get(), rsp.Size());
}
// 发送请求或广播数据到其他客户端的处理线程
void* msgbus_process_thread( void* param )
{
    while(true)
    {
        ReqTask reqtask;
        BroadcastTask broadcast_task;
        bool hasreqtask = false;
        bool hasbroadtask = false;
        {
            core::common::locker_guard guard(g_reqtask_locker);
            while(reqtoclient_task_container.empty() && broadcast_task_container.empty())
            {
                if( s_netmsgbus_server_terminate )
                    return 0;
                g_reqtask_cond.wait(g_reqtask_locker);
            }
            if( s_netmsgbus_server_terminate )
                return 0;
            if(!reqtoclient_task_container.empty())
            {
                reqtask = reqtoclient_task_container.front();
                reqtoclient_task_container.pop_front();
                hasreqtask = true;
            }
            if(!broadcast_task_container.empty())
            {
                broadcast_task = broadcast_task_container.front();
                broadcast_task_container.pop_front();
                hasbroadtask = true;
            }
        }
        if(hasreqtask)
        {
            TcpSockContainerT destclients;
            {
                core::common::locker_guard guard(g_activeclients_locker);
                ActiveClientTcpContainer::const_iterator cit = active_clients.begin();
                while(cit != active_clients.end())
                {
                    if(is_prefix_matching(cit->first, reqtask.client_name))
                    {
                        g_log.Log(lv_debug, "matching name %s vs %s", cit->first.c_str(), reqtask.client_name.c_str());
                        TcpSockSmartPtr destclient;
                        if(msgbus_select_best_client(cit->second, destclient))
                        {
                            destclients.push_back(destclient);
                        }
                    }
                    ++cit;
                }
            }
            for(size_t i = 0; i < destclients.size(); ++i)
            {
                if(destclients[i])
                    destclients[i]->SendData(reqtask.data.get(), reqtask.data_len);
            }
        }
        if(hasbroadtask)
        {
            ActiveClientTcpContainer allclients;
            {
                core::common::locker_guard guard(g_activeclients_locker);
                ActiveClientTcpContainer::const_iterator cit = active_clients.begin();
                while(cit != active_clients.end())
                {
                    TcpSockSmartPtr destclient;
                    if(msgbus_select_best_client(cit->second, destclient))
                    {
                        allclients[cit->first].push_back(destclient);
                    }
                    ++cit;
                }
            }
            ActiveClientTcpContainer::const_iterator allcit = allclients.begin();
            while( allcit != allclients.end() )
            {
                for(size_t i = 0;i < allcit->second.size(); ++i)
                {
                    if( allcit->second[i] )
                        allcit->second[i]->SendData(broadcast_task.data.get(), broadcast_task.data_len);
                }
                ++allcit;
            }
        }
    }
}

int main(int argc, char* argv[])
{
    if(argc > 1)
    {
        s_server_port = strtol(argv[1], NULL, 10);
        if(s_server_port == 0)
            s_server_port = MSGBUS_SERVER_DEFAULT_PORT;
    }
    else
    {
        s_server_port = MSGBUS_SERVER_DEFAULT_PORT;
    }
    threadpool::init_thread_pool();
    // message bus will offer two tcp connection, one for the register of a service, 
    // another for request an exist service.
    pthread_t register_thread;
    pthread_t process_thread;
    SimpleLogger::Instance().Init(utility::GetModulePath() + "/msgbus_server_log.log", lv_debug);

    s_netmsgbus_server_terminate = false;
    if (0 != pthread_create(&register_thread, NULL, msgbus_server_accept_thread,NULL))
    {
        g_log.Log(lv_error, "msgbus_register_thread create failed!" );
        return -1;
    }
    if (0 != pthread_create(&process_thread, NULL, msgbus_process_thread,NULL))
    {
        g_log.Log(lv_error, "msgbus_process_thread create failed!" );
        return -1;
    }

    while(!s_netmsgbus_server_terminate)
    {
        sleep(1);
    }

    {
        core::common::locker_guard guard(g_reqtask_locker);
        g_reqtask_cond.notify_all();
    }
    pthread_join(register_thread, NULL);
    pthread_join(process_thread, NULL);

    g_log.Log(lv_debug, "msgbus server is down!");
    threadpool::destroy_thread_pool();

    return 0;
}

