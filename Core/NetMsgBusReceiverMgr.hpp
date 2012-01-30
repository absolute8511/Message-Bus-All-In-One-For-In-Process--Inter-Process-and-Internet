#ifndef  NETMSGBUS_RECEIVER_MGR_H
#define  NETMSGBUS_RECEIVER_MGR_H

#include "NetMsgBusUtility.hpp"
#include "EventLoopPool.h"

//#if defined (__APPLE__) || defined (__MACH__) 
//#include "SelectWaiter.h"
//#else
//#include "EpollWaiter.h"
//#endif

#include "TcpSock.h"
#include "CommonUtility.hpp"
#include "threadpool.h"

#include <map>
#include <string>
#include <netinet/in.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <boost/shared_array.hpp>
#include <boost/bind.hpp>

using std::string;
#define TIMEOUT_SHORT 5
#define MAX_SENDMSG_CLIENT_NUM 1024

using namespace core::net;

namespace NetMsgBus
{

class ReceiverMgr
{
public:
    ReceiverMgr()
        :m_receiver_running(false),
        m_receiver_terminate(false)
    {
    }
    // start receiver at local.
    bool StartReceiver(unsigned short int& clientport)
    {
        // first to start local receiving server thread.
        if(!m_receiver_running)
        {
            m_localport = clientport;
            m_receiver_terminate = false;
            if(0 != pthread_create(&m_receiver_tid, NULL, process_data_from_other_clients_thread_func, 
                    (void*)this))
            {
                perror("start the receive thread error.\n");
                return false;
            }
            while(!m_receiver_running)
            {
                usleep(10);
            }
            clientport = m_localport;
            return true;
        }
        return false;
    }
    void StopReceiver()
    {
        m_receiver_terminate = true;
        if(m_receiver_running)
        {
            pthread_join(m_receiver_tid, NULL);
        }
    }

private:
    // 响应其他的客户端发送过来的消息数据
    static void* process_data_from_other_clients_thread_func(void *param)
    {
        ReceiverMgr* recv_mgr = (ReceiverMgr*)param; 
        if( recv_mgr == NULL )
        {
            throw;
            return 0;
        }
        int receive_server_sockfd;
        struct sockaddr_in rec_address;
        receive_server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
        // set SO_REUSEADDR in order to restart quickly after crash
        int optval = 1;
        setsockopt(receive_server_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

        rec_address.sin_family = AF_INET;
        rec_address.sin_addr.s_addr = htonl(INADDR_ANY);
        rec_address.sin_port = htons(recv_mgr->m_localport);

        int ret = -1;
        int retry = 10;
        while(ret == -1)
        {
            ret = bind(receive_server_sockfd, (sockaddr *)&rec_address, sizeof(rec_address));
            if(--retry < 0)
            {
                perror("failed to bind the port, start receiver mgr failed.");
                exit(1);
                return 0;
            }
            if(ret == -1)
            {
                ++recv_mgr->m_localport;
                rec_address.sin_port = htons(recv_mgr->m_localport);
                printf("retry next bind port:%d\n", recv_mgr->m_localport);
            }
        }
        listen(receive_server_sockfd, 5);
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(receive_server_sockfd, &readfds);

        if( !core::utility::set_fd_close_onexec(receive_server_sockfd))
        {
            perror("set fd close on exec error.\n");
            return 0;
        }

        if( !core::utility::set_fd_nonblock(receive_server_sockfd) )
        {
            perror("set receiver fd nonblock error.\n");
            return 0;
        }

        SockHandler callback;
        callback.onRead = boost::bind(&ReceiverMgr::receiver_onRead, recv_mgr, _1, _2, _3);
        callback.onSend = boost::bind(&ReceiverMgr::receiver_onSend, recv_mgr, _1);
        callback.onClose = boost::bind(&ReceiverMgr::receiver_onClose, recv_mgr, _1);
        callback.onError = boost::bind(&ReceiverMgr::receiver_onError, recv_mgr, _1);
        callback.onTimeout = boost::bind(&ReceiverMgr::receiver_onTimeout, recv_mgr, _1);

//#if defined (__APPLE__) || defined (__MACH__) 
//        boost::shared_ptr<SockWaiterBase> spwaiter(new SelectWaiter());
//#else
//        boost::shared_ptr<SockWaiterBase> spwaiter(new EpollWaiter());
//#endif
        //EventLoopPool::CreateEventLoop("msg_receiver_loop", spwaiter);
        // 这里先将正准备接收数据标记置位，如果注册接收服务失败，会把该标记位清除
        recv_mgr->m_receiver_running = true;
        //printf("client receiver waiting msgs from clients ...\n");

        recv_mgr->m_sendmsg_clientnum = 0;
        while(1)
        {
            fd_set testfds = readfds;
            struct timeval tv;
            tv.tv_sec = TIMEOUT_SHORT;
            tv.tv_usec = 0;

            int retcode;
            if(-1 == (retcode = select(receive_server_sockfd + 1, &testfds, 0, 0, &tv)))
            {
                perror("client receive server select error.\n");
                continue;
            }
            if(recv_mgr->m_receiver_terminate)
            {
                break;
            }
            if(retcode == 0)
            {// timeout
                continue;
            }
            if(FD_ISSET(receive_server_sockfd, &testfds))
            {
                // a new client connected and ready to send msgs to me.
                struct sockaddr_in client_address;
                int client_len = sizeof(client_address);
                int client_sockfd = accept(receive_server_sockfd, (struct sockaddr *)&client_address,
                    (socklen_t *)&client_len);
                if(client_sockfd >= 0)
                {
                    recv_mgr->m_sendmsg_clientnum++;
                    if(recv_mgr->m_sendmsg_clientnum > MAX_SENDMSG_CLIENT_NUM)
                    {
                        printf("------too many client connected. please wait for a while.-------\n");
                        close(client_sockfd);
                        continue;
                    }
                    //printf("a new client connected to sendmsg. fd = %d.\n", client_sockfd);
                    //printf("a new client connected to sendmsg:%lld\n", (int64_t)core::utility::GetTickCount());
                    char ip[INET_ADDRSTRLEN];
                    TcpSockSmartPtr newtcp(new TcpSock(client_sockfd, 
                            inet_ntop(AF_INET, &client_address.sin_addr, ip, sizeof(ip)), 
                            ntohs(client_address.sin_port)));
                    newtcp->SetSockHandler(callback);
                    newtcp->SetNonBlock();
                    newtcp->SetCloseAfterExec();
                    newtcp->SetTimeout(KEEP_ALIVE_TIME);
                    EventLoopPool::AddTcpSockToInnerLoop(newtcp);
                    //spwaiter->AddTcpSock(newtcp);
                }
            }
        }
        close(receive_server_sockfd);
        //EventLoopPool::TerminateLoop("msg_receiver_loop");
        recv_mgr->m_receiver_running = false;
        return 0;
    }

    // got a message from other client connection.
    size_t receiver_onRead(TcpSockSmartPtr sp_tcp, const char* pdata, size_t size)
    {
//        if(size > 0)
//        {
//            sp_tcp->SetTimeout(KEEP_ALIVE_TIME);
//        }
        size_t readedlen = 0;
        while(true)
        {
            char is_sync = 0;
            uint32_t data_len;
            size_t needlen;
            needlen = sizeof(is_sync);
            needlen += sizeof(data_len);
            if(size < needlen)
            {// not enough data, wait for next time
                return readedlen;
            }
            is_sync = *pdata;
            pdata += sizeof(is_sync);
            data_len = *((uint32_t*)pdata);
            pdata += sizeof(data_len);

            needlen += data_len;
            if(size < needlen)
            {
                return readedlen;
            }
            // 消息格式必须是 msgid=消息标示串＆msgparam=具体的消息内容 
            // 具体的消息内容可以是JSON/XML数据格式(或者也可以是二进制数据)，具体由收发双方协定
            // 第一次连接后必须先发一个包含msgsender的消息串表明自己的身份
            std::string msgcontent(pdata, data_len);
            //printf("got sendmsg data, syncflag:%d, content:%s, data_len:%d, string size:%ld.\n", (int)is_sync, pdata, data_len, msgcontent.size());
            // 身份验证,并过滤
            std::map<int, std::string>::iterator senderit = m_client_senders.find(sp_tcp->GetFD());
            if(senderit == m_client_senders.end())
            {
                //printf("check sender identity:%lld\n", (int64_t)core::utility::GetTickCount());
                std::string sendername;
                // cache is not exist, find in msgcontent
                if(!CheckMsgSender(msgcontent, sendername))
                {
                    // sender is missing or not allowed
                    size -= needlen;
                    readedlen += needlen;
                    pdata += data_len;
                    sp_tcp->DisAllowSend();
                    continue;
                }
                else
                {
                    m_client_senders[sp_tcp->GetFD()] = sendername;
                }
            }
            if(is_sync)
            {// 对方指定了同步等待回复，那么就直接调用消息处理函数后，把数据写回
                //printf("got a sync request on fd:%d.\n", sp_tcp->GetFD());
                //printf("got a sync request :%lld\n", (int64_t)core::utility::GetTickCount());
                threadpool::queue_work_task(boost::bind(NetMsgBusRspSendMsg, sp_tcp, msgcontent), 0);
            }
            else
            {// 异步的话，直接把消息抛给本机消息总线后立即返回
                NetMsgBusToLocalMsgBus(msgcontent);
            }
            size -= needlen;
            readedlen += needlen;
            pdata += data_len;
        }
    }

    bool receiver_onSend(TcpSockSmartPtr sp_tcp)
    {
        //sp_tcp->SetTimeout(KEEP_ALIVE_TIME);
        //printf("client %d, all data has been sended.\n", sp_tcp->GetFD());
        //m_sendmsg_clientnum--;
        //return false;
        return true;
    }

    void receiver_onClose(TcpSockSmartPtr sp_tcp)
    {
        //printf("client %d is to close.\n", sp_tcp->GetFD());
        m_sendmsg_clientnum--;
        m_client_senders.erase(sp_tcp->GetFD());
        return;
    }

    void receiver_onError(TcpSockSmartPtr sp_tcp)
    {
        perror("error:");
        printf("client %d , error happened.\n", sp_tcp->GetFD());
        receiver_onClose(sp_tcp);
        return;
    }
    void receiver_onTimeout(TcpSockSmartPtr sp_tcp)
    {
        //printf("client %d , timeout. in receiver.\n", sp_tcp->GetFD());
        receiver_onClose(sp_tcp);
        sp_tcp->Close();
        return;
    }

private:
    volatile bool m_receiver_running;
    volatile bool m_receiver_terminate;
    // the thread running to receive the message from other client.
    pthread_t m_receiver_tid;
    //EventLoopPool m_evpool;
    int m_sendmsg_clientnum;
    unsigned short int m_localport;
    // record the validate sender name of the tcp from client 
    std::map< int, std::string >  m_client_senders;
    static const int KEEP_ALIVE_TIME = 120000;
};

}
 #endif // NETMSGBUS_RECEIVER_MGR_H
