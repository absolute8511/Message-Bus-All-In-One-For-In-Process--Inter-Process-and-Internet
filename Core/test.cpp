#include "threadpool.h"
#include "msgbus_handlerbase.hpp"
#include "MsgHandlerMgr.h"
#include "NetMsgBusFilterMgr.h"
#include "EventLoopPool.h"
#include "TcpSock.h"
#include "SelectWaiter.h"
#include "SimpleLogger.h"

#include "xparam.hpp"
#include <inttypes.h>
#include <sys/types.h>
#include <stdio.h>
#include <boost/bind.hpp>
#include <string>
#include <signal.h>

using std::string;
using namespace NetMsgBus;
using namespace core;
static bool s_break = false;

static LoggerCategory g_log("test");
using namespace core::net;

static sigset_t maskset;
static void* sig_thread(void *arg)
{
    sigset_t *set = (sigset_t *)arg;
    int s,sig;
    for(;;){
        g_log.Log(lv_debug, "waiting signal...");
        s = sigwait(set, &sig);
        if(s != 0)
        {
            g_log.Log(lv_error, "sigwait signal error.");
            exit(1);
        }
        g_log.Log(lv_debug, "got signal : %d.", sig);
        switch(sig)
        {
        case SIGUSR1:
            g_log.Log(lv_debug, "got SIGUSR1 signal. ignored.");
            break;
        case SIGPIPE:
            g_log.Log(lv_debug, "got SIGPIPE signal. ignored.");
            break;
        case SIGINT:
            s_break = true;
            break;
        default:
            break;
        }
    }
}

void init_signals_env()
{
    //sigfillset(&maskset);
    sigemptyset(&maskset);
    //sigdelset(&maskset, SIGKILL);
    //sigdelset(&maskset, SIGSTOP);
    sigaddset(&maskset, SIGPIPE);
    sigaddset(&maskset, SIGINT);
    sigaddset(&maskset, SIGHUP);

    int ret;
    ret = pthread_sigmask(SIG_BLOCK, &maskset, NULL);

    if(ret != 0)
    {
        g_log.Log(lv_error, "pthread_sigmask set error.");
        exit(1);
    }

    pthread_t thread;
    ret = pthread_create(&thread, NULL, &sig_thread, (void*)&maskset);
    if(ret != 0)
    {
        g_log.Log(lv_error, "create signal handler thread failed.");
        exit(1);
    }

}


void testSyncGetData();

void GenerateNextTestParam(MsgBusParam& param, const string& longtestdata)
{
    int datavalue;
    core::XParam inxp;
    Param2CustomType(param, inxp);
    inxp.get_Int("testkey", datavalue);
    datavalue += 1;
    core::XParam xp;
    xp.put_Int("testkey", datavalue);
    xp.put_Str("testlongdata", longtestdata);
    param = CustomType2Param(xp);
}
void GenerateNextTestParam(MsgBusParam& param)
{
    int datavalue;
    core::XParam inxp;
    Param2CustomType(param, inxp);
    inxp.get_Int("testkey", datavalue);
    datavalue += 1;
    core::XParam xp;
    xp.put_Int("testkey", datavalue);
    param = CustomType2Param(xp);
}


class MyMsgHandlerClass : public MsgHandler<MyMsgHandlerClass>
{
public:
    static std::string ClassName()
    {
        return "MyMsgHandlerClass";
    }
    bool testMsgBus1(const std::string& msgid, MsgBusParam& param, bool& is_continue)
    {
        printMsg(msgid, param, __FUNCTION__);
        GenerateNextTestParam(param);
        NetMsgBusSendMsg("test.receiverclient_C", "rsp_msg_netmsgbus_testmsg1", param, SendDirectToClient);
        //NetMsgBusSendMsg("", "rsp_msg_netmsgbus_testmsg1", param, SendUseServerRelay);
        //sleep(1);
        return true;
    }
    bool testMsgBus2(const std::string& msgid, MsgBusParam& param, bool& is_continue)
    {
        printMsg(msgid, param, __FUNCTION__);
        GenerateNextTestParam(param);
        NetMsgBusSendMsg("test.receiverclient_C", "rsp_msg_netmsgbus_testmsg2", param, SendUseServerRelay);
        //NetMsgBusSendMsg("", "rsp_msg_netmsgbus_testmsg2", param, SendUseServerRelay);
        //sleep(1);
        return true;
    }
    bool testMsgBus3(const std::string& msgid, MsgBusParam& param, bool& is_continue)
    {
        //printMsg(msgid, param, __FUNCTION__);
        std::string rspstr("Yeah! I send the rsp data to you.");
        param = CustomType2Param(rspstr);
        //sleep(3);
        return true;
    }
    bool testRspRegReceiver(const std::string& msgid, MsgBusParam& param, bool& is_continue)
    {
        std::string name(param.paramdata.get(), param.paramlen);
        if(msgid == "netmsgbus.server.regreceiver.success")
        {
            printf("success receiver reg rsp :%s.\n", name.c_str());
        }
        else
        {
            printf("failed receiver reg rsp :%s.\n", name.c_str());
        }
        return true;
    }
    bool testCachedParam(const std::string& msgid, MsgBusParam& param, bool& is_continue)
    {
        m_test_cached_param.push_back(param);
        printf("caching param\n");
        //sleep(1);
        return true;
    }
    void printMsg(const std::string& msgid, MsgBusParam param, const std::string& func_name)
    {
        core::XParam xp;
        Param2CustomType(param, xp);
        int value = 0;
        string longdata;
        xp.get_Int("testkey", value);
        xp.get_Str("testlongdata", longdata);
        printf("process the (msg,param): (%s,%d,longdatasize:%zu) in %s , in thread:%lu.\n", msgid.c_str(), value, longdata.size(), func_name.c_str(), (unsigned long)pthread_self());
        //m_counter++;
        //if(m_counter % 100 == 0)
        //{
         //   printf("{%d} .\n", m_counter);
        //}
    }
    void printCachedParam()
    {
        for(size_t i = 0;i < m_test_cached_param.size();i++)
        {
            core::XParam xp;
            Param2CustomType(m_test_cached_param[i], xp);
            int value = 0;
            xp.get_Int("testkey", value);
            printf("test cached param : cached value (%d) \n", value);
        }
    }
    void InitMsgHandler()
    {
        m_counter = 0;
        AddHandler("msg_testMsgBus1", &MyMsgHandlerClass::testMsgBus1, 0);
        AddHandler("msg_testMsgBus2", &MyMsgHandlerClass::testMsgBus2, 0);
        AddHandler("msg_testMsgBus3", &MyMsgHandlerClass::testMsgBus3, 0);
        AddHandler("msg_testCachedParam", &MyMsgHandlerClass::testCachedParam, 0);

        AddHandler("netmsgbus.server.regreceiver.success", &MyMsgHandlerClass::testRspRegReceiver, 0);
        AddHandler("netmsgbus.server.regreceiver.success", &MyMsgHandlerClass::testRspRegReceiver, 0);
    }
    uint32_t m_counter;
    std::vector<MsgBusParam> m_test_cached_param;
};
typedef boost::shared_ptr<MyMsgHandlerClass> MyMsgHandlerClassPtr;
class MyMsgHandlerClass2 : public MsgHandler<MyMsgHandlerClass2>
{
public:
    static std::string ClassName()
    {
        return "MyMsgHandlerClass2";
    }
    bool testMsgBus21(const std::string& msgid, MsgBusParam& param, bool& is_continue)
    {
        printMsg(msgid, param, __FUNCTION__);

        //sleep(2);
        return true;
    }
    bool testMsgBus22(const std::string& msgid, MsgBusParam& param, bool& is_continue)
    {
        printMsg(msgid, param, __FUNCTION__);
        //sleep(1);
        return true;
    }
    bool testMsgBus23(const std::string& msgid, MsgBusParam& param, bool& is_continue)
    {
        printMsg(msgid, param, __FUNCTION__);
        //sleep(1);
        return true;
    }
    void printMsg(const std::string& msgid, MsgBusParam param, const std::string& func_name)
    {
        core::XParam xp;
        Param2CustomType(param, xp);
        int value = 0;
        xp.get_Int("testkey", value);
        printf("process the (msg,param): (%s,%d) in %s , in thread:%lu.\n", msgid.c_str(), value, func_name.c_str(), (unsigned long)pthread_self());
    }
    void InitMsgHandler()
    {
        AddHandler("msg_testMsgBus1", &MyMsgHandlerClass2::testMsgBus21, 0);
        AddHandler("msg_testMsgBus2", &MyMsgHandlerClass2::testMsgBus22, 0);
        AddHandler("msg_testMsgBus3", &MyMsgHandlerClass2::testMsgBus23, 1);
    }
};

typedef boost::shared_ptr<MyMsgHandlerClass2> MyMsgHandlerClass2Ptr;

void func1()
{
    printf("task 1 in thread:%lu.\n", (unsigned long)pthread_self());
    printf("task1 running.\n");
    sleep(1);
    printf("task1 waked.\n");
}
void func2(int arg)
{
    printf("task 2 in thread:%lu.\n", (unsigned long)pthread_self());
    printf("task2 arg is %d.\n",arg);
    ++arg;
    printf("task2 arg now is %d.\n",arg);
}
void func3(int arg1,int arg2)
{
    printf("task 3 in thread:%lu.\n", (unsigned long)pthread_self());
    printf("task3 arg1+arg2=%d.\n",arg1+arg2);
    arg1++;
    arg2++;
    sleep(15);
    printf("task3 arg1+arg2=%d.\n",arg1+arg2);
}
void func4(const string& str)
{
    printf("task 4 in thread:%lu.\n", (unsigned long)pthread_self());
    printf("task4 %s\n",str.c_str());
}

void waitforbreak()
{
    printf("wait for break...\n");
    while(getchar() != 'q')
    {
        sleep(1);
    }
    s_break = true;
}

void testthreadpool()
{
    threadpool::init_thread_pool();
    threadpool::task_type task1 = boost::bind(func1);
    //threadpool::queue_work_task(task1,0);
    threadpool::queue_work_task_to_named_thread(task1, "mynamed1");
    threadpool::queue_work_task_to_named_thread(boost::bind(func3,4,5),"mynamed1");
    task1 = boost::bind(func2,10);
    int id = threadpool::queue_timer_task(task1,1,false);
    threadpool::queue_work_task(boost::bind(func3,4,5),1);
    threadpool::queue_timer_task(boost::bind(func4,"1234567"),3,true);
    sleep(10);
    threadpool::queue_work_task(task1,0);
    threadpool::queue_timer_task(boost::bind(func4,"1234567"),2,true);
    threadpool::queue_work_task(boost::bind(func3,4,5),1);
    threadpool::queue_work_task_to_named_thread(boost::bind(func3,4,5),"mynamed2");
    threadpool::queue_work_task_to_named_thread(task1,"mynamed2");
    threadpool::terminate_named_thread("mynamed2");
    threadpool::queue_timer_task(boost::bind(func4,"1234567"),3,true);
    threadpool::queue_work_task(task1,0);
    id = threadpool::queue_timer_task(task1,1,false);
    threadpool::queue_timer_task(boost::bind(func4,"12345677654321"),3,true);
    sleep(5);
}
void testlocalmsgbus()
{
    sleep(1);
    MyMsgHandlerClass2Ptr testmsgbus2;
    MsgHandlerMgr::GetInstance(testmsgbus2);
    //MsgBusParam param = BuildinType2Param(13);
    core::XParam xp;
    xp.put_Int("testkey", 13);
    MsgBusParam param = CustomType2Param(xp);
    SendMsg("msg_testMsgBus3", param);
    /*GenerateNextTestParam(param);
    PostMsg("msg_testMsgBus1", param);
    GenerateNextTestParam(param);
    PostMsg("msg_testMsgBus2", param);
    GenerateNextTestParam(param);*/
    {
        SendMsg("msg_testMsgBus1", param);
        GenerateNextTestParam(param);
        PostMsg("msg_testMsgBus1", param);
        GenerateNextTestParam(param);
        PostMsg("msg_testMsgBus2", param);
        GenerateNextTestParam(param);
        MyMsgHandlerClassPtr testmsgbus;
        MsgHandlerMgr::GetInstance(testmsgbus);
        SendMsg("msg_testCachedParam", param);
        GenerateNextTestParam(param);
        SendMsg("msg_testMsgBus2", param);
        GenerateNextTestParam(param);
        SendMsg("msg_testMsgBus3", param);
        for(int i= 0; i < 30; i++)
        {
            GenerateNextTestParam(param);
            PostMsg("msg_testMsgBus2", param);
        }
        sleep(3);
        GenerateNextTestParam(param);
        PostMsg("msg_testCachedParam", param);
        GenerateNextTestParam(param);
        SendMsg("msg_testMsgBus1", param);
        GenerateNextTestParam(param);
        PostMsg("msg_testMsgBus2", param);
        GenerateNextTestParam(param);
        PostMsg("msg_testMsgBus3", param);
        GenerateNextTestParam(param);
        sleep(3);
        printAllMsgHandler("msg_testMsgBus1");
        testmsgbus->RemoveHandler("msg_testMsgBus1");
        SendMsg("msg_testMsgBus1", param);
        GenerateNextTestParam(param);
        SendMsg("msg_testMsgBus2", param);
        GenerateNextTestParam(param);
        testmsgbus->printCachedParam();
    }
    /*PostMsg("msg_testMsgBus2", param);
    GenerateNextTestParam(param);
    SendMsg("msg_testMsgBus1", param);
    GenerateNextTestParam(param);
    // sendmsg will change the param data.
    SendMsg("msg_testMsgBus3", param);*/
    GenerateNextTestParam(param);
    PostMsg("msg_testMsgBus2", param);
    GenerateNextTestParam(param);
    SendMsg("msg_testMsgBus1", param);
    printAllMsgHandler("msg_testMsgBus1");
    sleep(2);
}

void testremotemsgbus_broadcast_sub(MsgBusParam& param, const std::string& longdata)
{
    GenerateNextTestParam(param, longdata);
    // 测试广播消息
    NetMsgBusSendMsg("", "msg_netmsgbus_testmsg1", param, SendDirectToClient);

    GenerateNextTestParam(param);
    // 测试群组消息, 通过客户端直接发送预期失败
    //NetMsgBusSendMsg("test.", "msg_netmsgbus_testmsg2", param, SendDirectToClient);
    //NetMsgBusSendMsg("test.", "msg_netmsgbus_testmsg1", param, SendDirectToClient);

    GenerateNextTestParam(param, longdata);
    // 测试群组消息，通过服务器可以发送群组消息
    NetMsgBusSendMsg("test.", "msg_netmsgbus_testmsg2", param, SendUseServerRelay);
    NetMsgBusSendMsg("test.", "msg_netmsgbus_testmsg1", param, SendUseServerRelay);
}

void testremotemsgbus_direct_sub(MsgBusParam& param, const std::string& longdata)
{
    // 测试向指定的接收者发送消息
    GenerateNextTestParam(param);
    NetMsgBusSendMsg("test.receiverclient_A", "msg_netmsgbus_testmsg2", param, SendDirectToClient);
    NetMsgBusSendMsg("test.receiverclient_A", "msg_netmsgbus_testmsg1", param, SendDirectToClient);

    GenerateNextTestParam(param, longdata);
    NetMsgBusSendMsg("test.receiverclient_B", "msg_netmsgbus_testmsg2", param, SendDirectToClient);
    NetMsgBusSendMsg("test.receiverclient_B", "msg_netmsgbus_testmsg1", param, SendDirectToClient);
}

void testremotemsgbus_relay_sub(MsgBusParam& param, const std::string& longdata)
{
    GenerateNextTestParam(param);
    NetMsgBusSendMsg("test.receiverclient_A", "msg_netmsgbus_testmsg2", param, SendUseServerRelay);
    NetMsgBusSendMsg("test.receiverclient_A", "msg_netmsgbus_testmsg1", param, SendUseServerRelay);
    GenerateNextTestParam(param);
    NetMsgBusSendMsg("test.receiverclient_B", "msg_netmsgbus_testmsg2", param, SendUseServerRelay);
    NetMsgBusSendMsg("test.receiverclient_B", "msg_netmsgbus_testmsg1", param, SendUseServerRelay);
}

void testremotemsgbus_sync_sub(MsgBusParam& param)
{
    //GenerateNextTestParam(param);
    std::string rsp_content;
    //printf("begin get data:%lld\n", (int64_t)core::utility::GetTickCount());
    bool success = NetMsgBusGetData("test.receiverclient_A", "msg_netmsgbus_testgetdata",
        param, rsp_content, 2);
    if(success)
    {
        //printf("end get data:%lld\n", (int64_t)core::utility::GetTickCount());
        //printf("use netmsgbus get net data success in thread:%llu, data:%s.\n", (uint64_t)pthread_self(), rsp_content.c_str());
        //if(mintimeout > 1)
        //  --mintimeout;
        //sleep(3);
    }
    else
    {
        g_log.Log(lv_debug, "timeout(%d) err get net data in thread:%llu", (uint64_t)pthread_self());
        s_break = true;
    }
}

// 测试远程消息总线，跨进程跨机器等
void testremotemsgbus()
{
    printf("start as : (a for sender or b for receiverA or c for receiverB):\n");
    char inputflag = getchar();
    if(0 != NetMsgBusConnectServer("127.0.0.1",19000))
    {
        printf("connect msgbus server error.\n");
        DestroyMsgBus();
        return;
    }

    NetMsgBusQueryServices("");

    //bool ret = threadpool::queue_work_task(boost::bind(waitforbreak),0);
    //assert(ret);

    MyMsgHandlerClassPtr thandlerobj;
    MsgHandlerMgr::GetInstance(thandlerobj);
    if(inputflag == 'a' || inputflag == 'b' || inputflag == 'c')
    {
        std::string service_name;
        unsigned short int clientport;
        if(inputflag == 'a')
        {
            service_name = "test.receiverclient_A";
            thandlerobj->AddHandler("msg_netmsgbus_testmsg1", &MyMsgHandlerClass::testMsgBus1, 0); 
            thandlerobj->AddHandler("msg_netmsgbus_testmsg2", &MyMsgHandlerClass::testMsgBus2, 0);
            thandlerobj->AddHandler("msg_netmsgbus_testgetdata", &MyMsgHandlerClass::testMsgBus3, 0);
            clientport = 9100;
        }
        else if(inputflag == 'b')
        {
            service_name = "test.receiverclient_B";
            thandlerobj->AddHandler("msg_netmsgbus_testmsg1", &MyMsgHandlerClass::testMsgBus1, 0); 
            thandlerobj->AddHandler("msg_netmsgbus_testmsg2", &MyMsgHandlerClass::testMsgBus2, 0);
            clientport = 9101;
        }
        else if(inputflag == 'c')
        {
            //FilterMgr::AddIncludeSender("test.receiverclient_A");
            //FilterMgr::AddExcludeMsgId("rsp_msg_netmsgbus_testmsg2");
            service_name = "test.receiverclient_C";
            thandlerobj->AddHandler("rsp_msg_netmsgbus_testmsg1", &MyMsgHandlerClass::testMsgBus3, 0);
            thandlerobj->AddHandler("rsp_msg_netmsgbus_testmsg2", &MyMsgHandlerClass::testMsgBus3, 0);
            clientport = 9102;
        }
        //if( 0 != NetMsgBusRegReceiver(service_name, "127.0.0.1", clientport))
        if( 0 != NetMsgBusRegReceiver(service_name, "", clientport))
        {
            printf("register receiver client error.\n");
            return;
        }
    }
    else if(inputflag == 's')
    {
        //thandlerobj->AddHandler("rsp_msg_netmsgbus_testmsg1", &MyMsgHandlerClass::testMsgBus1, 0);
        //thandlerobj->AddHandler("rsp_msg_netmsgbus_testmsg2", &MyMsgHandlerClass::testMsgBus2, 0);
        unsigned short suggest_port = 0;
        if( 0 != NetMsgBusRegReceiver("test.onlyformsgbus_server_conn", "", suggest_port))
        {
            printf("register only msgbus server connection client error.\n");
            return;
        }
        printf("press any key other than 'q' to start send test message to netmsgbus.\n");
        core::XParam xp;
        xp.put_Int("testkey", 100);
        string longdata;
        for(int i = 0; i < 80; i++)
        {
            longdata.push_back(char(i%180 + 32));
        }
        xp.put_Str("testlongdata", longdata);
        MsgBusParam param = CustomType2Param(xp);
        uint32_t sendcounter = 0;

        
        NetMsgBusQueryHostInfo("test.receiverclient_A");
        sleep(3);
        int mintimeout = 3;

        threadpool::task_type t = boost::bind(testSyncGetData);
        for(int cocurrent = 0; cocurrent < 100; ++cocurrent)
        {
            threadpool::queue_work_task(t, 1);
        }
        printf(" in main start\n ");
        int64_t starttime = utility::GetTickCount();
        while(true)
        {
            if(s_break)
                break;
            //testremotemsgbus_broadcast_sub(param, longdata);
            //testremotemsgbus_direct_sub(param, longdata);
            //testremotemsgbus_relay_sub(param, longdata);
            
            sendcounter++;
            if(sendcounter % 100 == 0)
            {
                printf(" in main {%d}\n ", sendcounter);
                if(sendcounter >= 10000)
                    break;
            }
            //testremotemsgbus_sync_sub(param);            
        }
        printf("\n");
        int64_t endtime = utility::GetTickCount();
        g_log.Log(lv_debug, "%d msgs used time:%lld, (start,end): (%lld,%lld)\n", sendcounter, endtime - starttime, starttime,endtime);
        //
    }

    while(true)
    {
        if(s_break)
            break;
        sleep(1);
    }
    NetMsgBusDisConnect();
}

void testSyncGetData()
{
    core::XParam xp;
    xp.put_Int("testkey", 100);
    string longdata;
    for(int i = 0; i < 80; i++)
    {
        longdata.push_back(char(i%180 + 32));
    }
    xp.put_Str("testlongdata", longdata);
    MsgBusParam param = CustomType2Param(xp);
    int cnt = 0;
    printf("in thread start\n");
    int64_t starttime = utility::GetTickCount();
    while(true)
    {
        if(s_break)
            break;
        ++cnt;
        testremotemsgbus_sync_sub(param);
        if(cnt % 500 == 0)
        {
            printf("get net data success in thread:%llu, cnt:%d.\n", (uint64_t)pthread_self(), cnt);
        }
        if(cnt % 100 == 0)
        {
            if(starttime + 100000 < utility::GetTickCount())
                break;
        }
    }
    int64_t endtime = utility::GetTickCount();
    g_log.Log(lv_warn, "get net data in thread:%llu, total cnt:%d. used time:%lld, (start,end):(%lld,%lld)\n", (uint64_t)pthread_self(), cnt, endtime - starttime, starttime, endtime);
}

void testXParam()
{
    using namespace core;
    core::XParam xp;
    core::XParam xpxp;
    xp.put_Int("k1", 10);
    xp.put_WStr("k2", L"wstr宽字符test");
    xp.put_Str("k3", "ansi普通字符test");
    xp.put_Bool("k4", true);
    xp.put_Real("k5", 21.23789);
    xp.put_UInt("k6", 3000000000);
    xp.put_UInt("k7", 30);
    long long int longtest = 9999999999999999;
    xp.put_ULongLong("k8", longtest);
    long long int longesttest = 0xffffffffffffffff;
    xp.put_ULongLong("k9", longesttest);
    //xp.put_Array("k6",);
    //xp.put_Obj("k7",);
    xpxp.put_XParam("k11", xp);
    string chararray("123");
    chararray += '\0';
    chararray += '\0';
    chararray += "321";
    xp.put_CharArray("k12", chararray);
    MsgBusParam tmpparam = xp.ToMsgBusParam();
    MsgBusParam tmpxpxpparam = xpxp.ToMsgBusParam();

    core::XParam xp2;
    bool ret = xp2.FromMsgBusParam(tmpparam);
    assert(ret);

    core::XParam xparray;
    std::vector<core::XParam> xpvec;
    xpvec.push_back(xp);
    xpvec.push_back(xp2);
    xparray.put_XParamVector("karray1", xpvec);
    MsgBusParam tmpxparrayparam = xparray.ToMsgBusParam();
    std::map<string, XParam> xpmap;
    core::XParam xpobj;
    xpmap["kmap1"] = xp;
    xpmap["kmap2"] = xparray;
    xpobj.put_XParamMap("kmaproot", xpmap);
    MsgBusParam tmpxpmapparam = xpobj.ToMsgBusParam();
    core::XParam xpobj2;
    ret = xpobj2.FromMsgBusParam(tmpxpmapparam);
    assert(ret);
    std::map<string, XParam> xpmap2;
    xpobj2.get_XParamMap("kmaproot", xpmap2);

    printf("%s\n", xpobj2.ToJsonStr().c_str());
    assert(xp.ToJsonStr() == xpmap2["kmap1"].ToJsonStr());
    std::vector<XParam> xpvec2;
    xpmap2["kmap2"].get_XParamVector("karray1", xpvec2);
    assert(xp2.ToJsonStr() == xpvec2[1].ToJsonStr());

    int32_t intres = 0;
    xp2.get_Int("k1", intres);
    assert(intres == 10);
    std::wstring wstrres;
    xp2.get_WStr("k2", wstrres);
    assert(wstrres == L"wstr宽字符test");
    std::string strres;
    xp2.get_Str("k3", strres);
    assert(strres == "ansi普通字符test");
    bool boolres = false;
    xp2.get_Bool("k4", boolres);
    assert(boolres);
    double realres;
    xp2.get_Real("k5", realres);
    assert(realres == 21.23789);
    uint32_t uintres = 0;
    xp2.get_UInt("k6", uintres);
    assert(uintres == 3000000000);
    xp2.get_UInt("k7", uintres);
    assert(uintres == 30);
    uint64_t longres;
    xp2.get_ULongLong("k8", longres);
    assert(longres == 9999999999999999);
    xp2.get_ULongLong("k9", longres);
    assert((uint64_t)longres == 0xffffffffffffffff);
    string chararrayres;
    xp2.get_CharArray("k12", chararrayres);
    assert(chararray == chararrayres);
    printf("------xparam test pass!-----\n");
}

class TestEventLoop
{
private:
    //EventLoopPool m_evpool;
    TcpSockSmartPtr m_pConnection;
public:
    TestEventLoop()
    {
        boost::shared_ptr<SockWaiterBase> spwaiter(new SelectWaiter());
        EventLoopPool::CreateEventLoop("test_event_loop", spwaiter);
    }
    size_t test_onRead(TcpSockSmartPtr sp, const char* pdata, size_t size)
    {
        printf("on read %zu\n", size);
        return size;
    }
    bool test_onSend(TcpSockSmartPtr sp)
    {
        printf("on send \n");
        return true;
    }
    void test_onError(TcpSockSmartPtr sp)
    {
        printf("on error\n");
    }
    void test_onClose(TcpSockSmartPtr sp)
    {
        printf("on close\n");
    }
    bool testLoop()
    {
        boost::shared_ptr<SockWaiterBase> spwaiter;
        if(!EventLoopPool::GetEventLoop("test_event_loop"))
        {
            return false;
        }
        spwaiter = EventLoopPool::GetEventLoop("test_event_loop")->GetEventWaiter();
        if(!spwaiter)
        {
            return false;
        }
        m_pConnection.reset(new TcpSock());
        if (NULL == m_pConnection)
        {
            return false;
        }
        SockHandler callback;
        callback.onRead = boost::bind(&TestEventLoop::test_onRead, this, _1, _2, _3);
        callback.onSend = boost::bind(&TestEventLoop::test_onSend, this, _1);
        callback.onError = boost::bind(&TestEventLoop::test_onError, this, _1);
        callback.onClose = boost::bind(&TestEventLoop::test_onClose, this, _1);
        m_pConnection->SetSockHandler(callback);
        //printf("begin connect fts server. ip:port=%s:%d, timeout=%d\n", strFtsAddr.c_str(),
        //    wFtsPort, nTimeout);
        struct timeval tv;
        tv.tv_sec = 3; 
        tv.tv_usec = 0;
        bool connected = m_pConnection->Connect("127.0.0.1", 9000, tv);
        if(!connected)
        {
            perror("connect test failed.");
            return false;
        }
        printf("connected.\n");
        m_pConnection->SetNonBlock();
        m_pConnection->SetCloseAfterExec();
        spwaiter->AddTcpSock(m_pConnection);
        return true;
    }
};

void testeventloop()
{
    TestEventLoop test;
    test.testLoop();
}

void testSimpleLogger()
{
    int pid = getpid();
    int i = 100;
    while(--i > 0)
    {
    time_t t = time(NULL);
    int64_t tl = (int64_t)t;
    g_log.Log(lv_debug, "test lv_debug time_t printf %lld, pid:%d", (int64_t)t, pid);
    g_log.Log(lv_info, "test lv_info time_t printf %lld, pid:%d", (int64_t)t, pid);
    g_log.Log(lv_error, "test lv_error time_t printf %lld, pid:%d", (int64_t)t, pid);
    usleep(100000);
    }
    sleep(3);
}

int main()
{
    init_signals_env();
    using namespace core;
    //SimpleLogger::Instance().Init("/Users/absolute/workspace/aliwwforlinux/Bin/testlog.log", lv_debug);
    SimpleLogger::Instance().Init("./testlog.log", lv_debug);
    threadpool::init_thread_pool();
    EventLoopPool::InitEventLoopPool();
    InitMsgBus(0);
    printf("main in thread: %lld.\n",(uint64_t)pthread_self());
    //testSimpleLogger();
    //sleep(30000);
    //testthreadpool();
    //testXParam();
    //testeventloop();
    //threadpool::queue_work_task(boost::bind(testlocalmsgbus), 0);
    //threadpool::queue_work_task(boost::bind(testlocalmsgbus), 1);
    //testlocalmsgbus();
    testremotemsgbus();
    MsgHandlerMgr::DropAllInstance();
    EventLoopPool::DestroyEventLoopPool();
    DestroyMsgBus();
    threadpool::destroy_thread_pool();
    printf("leave thread pool and msgbus.\n");
}
