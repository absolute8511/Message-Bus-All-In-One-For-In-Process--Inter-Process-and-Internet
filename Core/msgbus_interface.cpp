#include "msgbus_interface.h"
#include "lock.hpp"
#include "condition.hpp"
#include "threadpool.h"
#include "msgbus_client.h"
#include "SimpleLogger.h"
#include "NetMsgBusUtility.hpp"

#include <pthread.h>
#include <list>
#include <boost/bind.hpp>
#include <map>
#include <deque>
#include <stdio.h>
#include <errno.h>

#define TIMEOUT_SENDMSG  15

using namespace core;
static LoggerCategory g_log("msgbus_interface");

namespace NetMsgBus
{
// 消息总线的消息处理线程
void* MsgTaskProcessProc(void*);

/*
 *struct MsgHandlerWrapper
 *{
 *    MsgHandlerWrapper(const std::string& lmsgid, IMsgHandler* phandler)
 *        :msgid(lmsgid),
 *        p_handler_obj(phandler)
 *    {
 *    }
 *    const std::string& msgid;
 *    IMsgHandler*  p_handler_obj;
 *
 *};
 */

// 某个消息的处理对象列表类型
typedef std::list<MsgHandlerWeakRef> MsgHandlerWeakObjList;
typedef std::list<MsgHandlerStrongRef> MsgHandlerStrongObjList;
// 所有消息的处理对象总集合类型
typedef std::map< std::string, MsgHandlerWeakObjList > MsgHandlerObjContainerT;
static MsgHandlerObjContainerT s_all_msghandler_objs;

// 用于向消息总线添加消息的结构
struct MsgTask
{
    MsgTask()
        :msgid(""),
        msgparam(),
        callertid(0)
    {
    }
    MsgTask(const std::string& lmsgid, MsgBusParam lparam, pthread_t fromtid)
        :msgid(lmsgid),
        msgparam(lparam),
        callertid(fromtid)
    {
    }
    std::string msgid;
    MsgBusParam msgparam;
    pthread_t callertid;
};


template<> MsgBusParam CustomType2Param(const std::string& src)
{
    boost::shared_array<char> paramdata(new char[src.size()]);
    memcpy(paramdata.get(), src.data(), src.size());
    MsgBusParam msgparam(paramdata, src.size());
    return msgparam;
}
template<> void Param2CustomType(MsgBusParam param, std::string& result)
{
    result = std::string(param.paramdata.get(), param.paramlen);
}


// 消息总线存储消息的队列类型
typedef std::deque<MsgTask> MsgTaskQueue;
// 存储了所有的还没处理的消息
static MsgTaskQueue s_all_msgtask;
// 需要等待同步处理的消息
static MsgTaskQueue s_all_sendmsgtask;
// 当前的UI线程，用于向UI线程发消息
static long s_gui_hwnd;
// 消息总线是否正在运行的标记
static volatile bool s_msgbus_running = false;
static volatile bool s_msgbus_terminate = false;
// 消息总线处理线程ID
static pthread_t msgbus_tid;

// 用于保护消息队列的锁和用于通知消息队列的事件
static core::common::locker s_msgtask_locker;
static core::common::condition s_msgtask_condition;
// 用于保护消息处理对象集合的锁
static core::common::locker s_msghandlers_locker;
//
struct ReadySendMsgInfo
{
    bool ready;
    MsgBusParam rspparam;
    bool  rspresult;
};

// 记录同步发送消息的结果,对应不同线程,唤醒对应的等待线程
typedef std::map<pthread_t, ReadySendMsgInfo>  ReadySendMsgResultT;
ReadySendMsgResultT  s_ready_sendmsgs;
core::common::condition s_ready_sendmsg_cond;
core::common::locker  s_ready_sendmsg_locker;

static bool SendMsgInMsgBusThread(const std::string& msgid, MsgBusParam& param);

// 启动线程池以及消息处理线程
bool InitMsgBus(long hmainwnd)
{
    if( s_msgbus_running )
        return true;
    s_gui_hwnd = hmainwnd;
    s_msgbus_terminate = false;
    if(0 != pthread_create(&msgbus_tid, NULL, MsgTaskProcessProc, NULL))
    {
        return false;
    }
    while(!s_msgbus_running)
    {
        usleep(10);
    }
    g_log.Log(lv_debug, "msgbus thread id :%lld", (uint64_t)msgbus_tid);
    return true;
}
// 销毁消息处理线程, 线程池不销毁，因为可能其他地方也在用
void DestroyMsgBus()
{
    s_msgbus_terminate = true;
    {
        core::common::locker_guard guard(s_msgtask_locker);
        s_msgtask_condition.notify_all();
    }
    pthread_join(msgbus_tid, NULL);
    s_all_msgtask.clear();
    s_all_sendmsgtask.clear();
    s_all_msghandler_objs.clear();
}

bool SendMsg(const std::string& msgid)
{
    MsgBusParam tmpparam = BuildinType2Param(0);
    return SendMsg(msgid, tmpparam);
}

bool PostMsg(const std::string& msgid)
{
    return PostMsg(msgid, BuildinType2Param(0));
}

bool SendMsg(const std::string& msgid, boost::shared_array<char>& param, uint32_t& paramlen)
{
    MsgBusParam msgparam(param, paramlen);
    bool ret = SendMsg(msgid, msgparam);
    param = msgparam.paramdata;
    paramlen = msgparam.paramlen;
    return ret;
}

bool PostMsg(const std::string& msgid, boost::shared_array<char> param, uint32_t paramlen)
{
    return PostMsg(msgid, MsgBusParam(param, paramlen));
}

// process message in msgbus thread 
static bool SendMsgInMsgBusThread(const std::string& msgid, MsgBusParam& param)
{
    assert(pthread_equal(pthread_self(), msgbus_tid));
    MsgHandlerStrongObjList msg_handlers;
    // 先将该消息的处理对象队列的强引用拿出来
    {
        core::common::locker_guard guard(s_msghandlers_locker);
        MsgHandlerObjContainerT::iterator it = s_all_msghandler_objs.find(msgid);
        if( it != s_all_msghandler_objs.end() )
        {
            MsgHandlerWeakObjList& weak_msg_handlers = it->second;
            MsgHandlerWeakObjList::iterator weakit = weak_msg_handlers.begin();
            while(weakit != weak_msg_handlers.end())
            {
                MsgHandlerStrongRef sh = weakit->lock();
                if(sh)
                {
                    // strong ref is validate
                    msg_handlers.push_back(sh);
                    ++weakit;
                }
                else
                {
                    // clear invalidate weakref.
                    g_log.Log(lv_warn, "removing invalidate weak ref.");
                    weakit = weak_msg_handlers.erase(weakit);
                }
            }
            if(weak_msg_handlers.empty())
            {
                s_all_msghandler_objs.erase(it);
            }
        }
    }
    MsgHandlerStrongObjList::const_iterator hit = msg_handlers.begin();
    bool result = false;
    bool is_continue = true;
    assert(param.paramlen);
    // make a copy of param to prevent interfering with other msg handlers.
    MsgBusParam original_param = param.DeepCopy();

    // 注：改造后的list是个强引用的list,list中的对象一定还有效
    while(hit != msg_handlers.end())
    {
        if(*hit != NULL)
        {
#ifndef NDEBUG
            time_t start_ = core::utility::GetTickCount();
#endif
            MsgBusParam input_param = original_param.DeepCopy();
            result = (*hit)->OnMsg(msgid, input_param, is_continue);
#ifndef NDEBUG
            time_t end_ = core::utility::GetTickCount();
            if( (end_ - start_) > 500 )
                g_log.Log(lv_debug, "===msg:%s process time is too long %lld ms.===", msgid.c_str(),
                    (int64_t)(end_ - start_));
#endif
            if(result)
            {
                param = input_param;
            }
        }
        if(!is_continue)
            break;
        ++hit;
    }

    return result;

}
// 同步处理，直接调用处理函数
bool SendMsg(const std::string& msgid, MsgBusParam& param)
{
    if( !s_msgbus_running )
    {
        g_log.Log(lv_debug, "send %s while msgbus not running", msgid.c_str());
        assert(s_msgbus_running);
        return false;
    }
    pthread_t callertid = pthread_self();
    if(pthread_equal(callertid, msgbus_tid) != 0)
    {
        return SendMsgInMsgBusThread(msgid, param);
    }
    {
        core::common::locker_guard guard(s_ready_sendmsg_locker);
        s_ready_sendmsgs[callertid].ready = false;
        s_ready_sendmsgs[callertid].rspresult = false;
    }
    struct timespec ts;
    ts.tv_sec = time(NULL) + TIMEOUT_SENDMSG;
    ts.tv_nsec = 0;
    bool ready = false;
    // push the task to the pop of the task queue. wait it to excute in the thread of the msgbus.
    {
        core::common::locker_guard guard(s_msgtask_locker);
        s_all_sendmsgtask.push_back(MsgTask(msgid, param, callertid));
        s_msgtask_condition.notify_one();
    }

    while(!ready)
    {
        {
            core::common::locker_guard guard(s_ready_sendmsg_locker);
            int retcode = s_ready_sendmsg_cond.waittime(s_ready_sendmsg_locker, &ts);
            if(retcode == ETIMEDOUT)
            {
                g_log.Log(lv_debug, "sendmsg ready wakeup for timeout. msgid:%s.", msgid.c_str());
                g_log.Log(lv_warn, "sendmsg ready wakeup for timeout. msgid:%s.", msgid.c_str());
                s_ready_sendmsgs.erase(callertid);
                return false;
            }
            ready = s_ready_sendmsgs[callertid].ready;
            g_log.Log(lv_debug, "one sync data waiter wakeup in tid:%lld. msgid:%s, ready:%d.",
               (uint64_t)callertid, msgid.c_str(), ready?1:0);
            if(ready)
            {
                param = s_ready_sendmsgs[callertid].rspparam;
                bool ret = s_ready_sendmsgs[callertid].rspresult;
                s_ready_sendmsgs.erase(callertid);
                return ret;
            }
        }
    }

    return false;
}

// 异步处理，放入消息队列后直接返回
bool PostMsg(const std::string& msgid, MsgBusParam param)
{
    if( !s_msgbus_running )
    {
        g_log.Log(lv_debug, "post %s while msgbus not running", msgid.c_str());
        assert(s_msgbus_running);
        return false;
    }
    assert(param.paramlen);
    core::common::locker_guard guard(s_msgtask_locker);
    // post message will be processed in the thread of msgbus.
    s_all_msgtask.push_back(MsgTask(msgid, param.DeepCopy(), msgbus_tid));
#ifndef NDEBUG
    if(s_all_msgtask.size() > 5)
        g_log.Log(lv_debug, "current msg size:%zu.", s_all_msgtask.size());
#endif
    s_msgtask_condition.notify_one();
    return true;
}
// 注册消息处理对象，注意同一个消息相同的处理对象只允许注册一次
bool RegisterMsg(const std::string& msgid, MsgHandlerStrongRef sp_handler_obj)
{
    assert(s_msgbus_running);
    if( !s_msgbus_running )
        return false;
    if( sp_handler_obj == NULL )
        return false;
    core::common::locker_guard guard(s_msghandlers_locker);
    MsgHandlerObjContainerT::iterator it = s_all_msghandler_objs.find(msgid);
    bool isexist = false;
    if(it != s_all_msghandler_objs.end())
    {
        MsgHandlerWeakObjList::iterator hit = it->second.begin();
        while(hit != it->second.end())
        {
            if(MsgHandlerStrongRef(hit->lock()) == sp_handler_obj)
            {
                //已经注册过
                isexist = true;
                break;
            }
            ++hit;
        }

    }
    // 该对象没有注册过该消息，则加到该消息的处理对象列表中去
    if(!isexist)
    {
        if(it == s_all_msghandler_objs.end())
        {
            MsgHandlerWeakObjList hlist;
            hlist.push_back(sp_handler_obj);
            s_all_msghandler_objs[msgid] = hlist;
        }
        else
        {
            it->second.push_back(sp_handler_obj);
        }
    }
    return true;
}
// 反注册消息
void UnRegisterMsg(const std::string& msgid, IMsgHandler* p_handler_obj)
{
    assert(s_msgbus_running);
    if( !s_msgbus_running )
        return;
    core::common::locker_guard guard(s_msghandlers_locker);
    MsgHandlerObjContainerT::iterator it = s_all_msghandler_objs.find(msgid);
    if(it != s_all_msghandler_objs.end())
    {
        MsgHandlerWeakObjList::iterator hit = it->second.begin();
        while(hit != it->second.end())
        {
            if(MsgHandlerStrongRef(hit->lock()).get() == p_handler_obj)
            {
                //找到了注册过的对象，删除
                //g_log.Log(lv_debug, "removing registered handler.");
                it->second.erase(hit);
                break;
            }
            ++hit;
        }
    }
}

void* MsgTaskProcessProc(void*)
{
    s_msgbus_running = true;
    while(true)
    {
        if( s_msgbus_terminate )
            break;
        MsgTask mtask;
        {
            core::common::locker_guard guard(s_msgtask_locker);
            while(s_all_msgtask.empty() && s_all_sendmsgtask.empty())
            {
                if(s_msgbus_terminate)
                {
                    s_msgbus_running = false;
                    return 0;
                }
                s_msgtask_condition.wait(s_msgtask_locker);
            }
            // process sendmsg first
            if(!s_all_sendmsgtask.empty())
            {
                mtask = s_all_sendmsgtask.front();
                s_all_sendmsgtask.pop_front();
            }
            else
            {
                mtask = s_all_msgtask.front();
                s_all_msgtask.pop_front();
            }
        }
        if(s_msgbus_terminate)
            break;
        // 在消息处理线程中以同步的方式处理消息
        bool ret = SendMsgInMsgBusThread(mtask.msgid, mtask.msgparam);
        if(pthread_equal(mtask.callertid, msgbus_tid) == 0)
        {// 非总线线程调用的, 需要通知调用线程消息已经处理完毕
            core::common::locker_guard guard(s_ready_sendmsg_locker);
            s_ready_sendmsgs[mtask.callertid].ready = true;
            s_ready_sendmsgs[mtask.callertid].rspparam = mtask.msgparam;
            s_ready_sendmsgs[mtask.callertid].rspresult = ret;
            g_log.Log(lv_debug, "notify sendmsg ready, tid:%lld, msg:%s", (uint64_t)mtask.callertid, mtask.msgid.c_str());
            s_ready_sendmsg_cond.notify_all();
        }
    }
    s_msgbus_running = false;
    return 0;
}
// 连接到网络消息总线服务器并将自己接收其他客户端消息的服务端口注册到消息总线
int  NetMsgBusConnectServer(const std::string& serverip, unsigned short int serverport)
{
    if(!init_netmsgbus_client(serverip, serverport))
        return 1;
    return 0;
}
int  NetMsgBusRegReceiver(const std::string& name, const std::string& hostip, unsigned short& hostport)
{
    if(!msgbus_register_client_receiver(hostip, hostport, name))
        return 2;
    return 0;
}

void NetMsgBusDisConnect()
{
    destroy_netmsgbus_client();
}
void NetMsgBusDisConnectFromClient(const std::string& name)
{
    msgbus_disconnect_receiver(name);
}

bool NetMsgBusSendMsg(const std::string& dest_name, const std::string& msgid,
   MsgBusParam param, kMsgSendType sendtype)
{
    // contruct the data which will be send to the network
    // data is like : msgid=XXXXXXXX&msgparam=XXXXXXXXXX
    assert(param.paramlen);
    std::string netmsg_str(param.paramdata.get(), param.paramlen);
    std::string encodemsgid = msgid;
    EncodeMsgKeyValue(encodemsgid);
    EncodeMsgKeyValue(netmsg_str);
    netmsg_str = "msgid=" + msgid + "&msgparam=" + netmsg_str;
    uint32_t netmsg_len = netmsg_str.size();
    boost::shared_array<char> netmsg_data(new char[netmsg_len]);
    memcpy(netmsg_data.get(), netmsg_str.data(), netmsg_len);

    if(dest_name == "")
    {
        g_log.Log(lv_debug, "broadcast msgid:%s", msgid.c_str());
        return msgbus_postmsg_broadcast(netmsg_len, netmsg_data);
    }
    else if(sendtype == SendDirectToClient)
    {
        return msgbus_postmsg_direct_to_client(dest_name, netmsg_len, netmsg_data);
    }
    else if(sendtype == SendUseServerRelay)
    {
        return msgbus_postmsg_use_server_relay(dest_name, netmsg_len, netmsg_data);
    }
    else
    {
        return false;
    }
    return true;
}
bool NetMsgBusQueryHostInfo(const std::string& clientname)
{
    return msgbus_req_receiver_info(clientname);
}

bool NetMsgBusGetData(const std::string& clientname, const std::string& msgid, MsgBusParam param, 
    std::string& rsp_data, int32_t timeout_sec)
{
    assert(param.paramlen);
    std::string netmsg_str(param.paramdata.get(), param.paramlen);
    std::string encodemsgid = msgid;
    EncodeMsgKeyValue(encodemsgid);
    EncodeMsgKeyValue(netmsg_str);
    netmsg_str = "msgid=" + msgid + "&msgparam=" + netmsg_str;
    uint32_t netmsg_len = netmsg_str.size();
    boost::shared_array<char> netmsg_data(new char[netmsg_len]);
    memcpy(netmsg_data.get(), netmsg_str.data(), netmsg_len);

    return msgbus_sendmsg_direct_to_client(clientname, netmsg_len, netmsg_data, rsp_data, timeout_sec);
}

int  NetMsgQueryServices(const std::string& match_str)
{
    return msgbus_query_available_services(match_str);
}

}
