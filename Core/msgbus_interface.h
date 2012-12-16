#ifndef MSGBUS_INTERFACE_H
#define MSGBUS_INTERFACE_H
#include <stdint.h>
#include <string>
#include <string.h>
#include <boost/shared_array.hpp>
#include <boost/weak_ptr.hpp>
#include <vector>
#include <map>

namespace NetMsgBus
{
    struct MsgBusParam
    {
        MsgBusParam()
            :paramdata(NULL),
            paramlen(0)
        {
        }
        MsgBusParam(boost::shared_array<char> data, uint32_t len)
            :paramdata(data),
            paramlen(len)
        {
        }
        MsgBusParam DeepCopy()
        {
            boost::shared_array<char> destcopy(new char[paramlen]);
            memcpy(destcopy.get(), paramdata.get(), paramlen);
            return MsgBusParam(destcopy, paramlen);
        }
        boost::shared_array<char> paramdata;
        uint32_t  paramlen;
    };

    class NetFuture;
    class IMsgHandler
    {
    public:
        virtual bool OnMsg(const std::string&, MsgBusParam& param, bool&) = 0;
        virtual ~IMsgHandler(){}
    protected:
        IMsgHandler(){}
    };
    typedef boost::shared_ptr<IMsgHandler> MsgHandlerStrongRef;
    typedef boost::weak_ptr<IMsgHandler> MsgHandlerWeakRef;
    template <typename T> MsgBusParam BuildinType2Param(const T& src)
    {
        boost::shared_array<char> paramdata(new char[sizeof(T)]);
        memcpy(paramdata.get(), (char*)&src, sizeof(T));
        MsgBusParam msgparam(paramdata, sizeof(T));
        return msgparam;
    }
    template <typename T> void Param2BuildinType(MsgBusParam param, T& result)
    {
        assert(param.paramlen == sizeof(T));
        memcpy((char*)&result, param.paramdata.get(), sizeof(T));
    }
    template <typename T> MsgBusParam CustomType2Param(const T& src)
    {
        return src.ToMsgBusParam();
    }
    template <typename T> void Param2CustomType(MsgBusParam param, T& result)
    {
        result.FromMsgBusParam(param);
    }

    // convert protocol buffer types to msgbusparam.
    template <typename T> MsgBusParam PBType2Param(const T& src)
    {
        int size = src.ByteSize();
        boost::shared_array<char> paramdata(new char[size]);
        src.SerializeToArray(paramdata.get(), size);
        MsgBusParam msgparam(paramdata, size);
        return msgparam;
    }
    template <typename T> void Param2PBType(MsgBusParam param, T& result)
    {
        result.ParseFromArray(param.paramdata.get(), param.paramlen);
    }

    template <typename T> void Param2CustomType(MsgBusParam param, std::vector<T>& result)
    {
        T::MsgBusParamToVectorType(param, result);
    }
    template <typename T> MsgBusParam CustomType2Param(const std::vector<T>& src)
    {
        return T::VectorTypeToMsgBusParam(src);
    }
    template <typename T> void Param2CustomType(MsgBusParam param, std::map<std::string, T>& result)
    {
        T::MsgBusParamToMapType(param, result);
    }
    template <typename T> MsgBusParam CustomType2Param(const std::map<std::string, T>& src)
    {
        return T::MapTypeToMsgBusParam(src);
    }

    template<> MsgBusParam CustomType2Param(const std::string& src);
    template<> void Param2CustomType(MsgBusParam param, std::string& result);
    
    enum kMsgSendType
    {
        // first try send to client directly, then try again when client host info updated from server.
        SendDirectToClient = 1,
        // just send data to server, and server will forward the message to dest client.
        SendUseServerRelay = 2,
    };
    // each process only can have one netmsgbus and one localmsgbus.
    // process can use the netmsgbus to communicate with other process, and then 
    // use netmsgbus to notify localmsgbus to handle the messages from other process.
    bool InitMsgBus(long hmainwnd);
    void DestroyMsgBus();
    bool SendMsg(const std::string& msgid);
    bool PostMsg(const std::string& msgid);
    bool SendMsg(const std::string& msgid, MsgBusParam& param);
    bool PostMsg(const std::string& msgid, MsgBusParam param);
    bool SendMsg(const std::string& msgid, boost::shared_array<char>& param, uint32_t& paramlen);
    bool PostMsg(const std::string& msgid, boost::shared_array<char> param, uint32_t paramlen);
    bool RegisterMsg(const std::string& msgid, MsgHandlerStrongRef sp_handler_obj, bool must_called_inmsgbusthread = true);
    void UnRegisterMsg(const std::string& msgid, IMsgHandler* p_handler_obj);
    // connect the netmsgbus server before do something related to netmsgbus.
    int  NetMsgBusConnectServer(const std::string& serverip, unsigned short int serverport);

    void NetMsgBusDisConnectFromServer();
    void NetMsgBusDisConnectAll();
    // register a receiver on the netmsgbus so that the client can receive messages from other client.
    int  NetMsgBusRegReceiver(const std::string& name, const std::string& hostip, unsigned short& hostport);
    // send messages to a client connected with netmsgbus server. Use empty dest_name to broadcast messages on the netmsgbus.
    // 
    bool NetMsgBusSendMsg(const std::string& dest_name, const std::string& msgid, MsgBusParam param, kMsgSendType sendtype);
    bool NetMsgBusSendMsg(const std::string& dest_ip, unsigned short dest_port, const std::string& msgid,
        MsgBusParam param);
    // if you have no more data to send, then you can disconnect from the receiver. 
    //void NetMsgBusDisConnectFromClient(const std::string& name);
    // get the ip and port info and cache them in client. just work like dns name resolve.
    bool NetMsgBusQueryHostInfo(const std::string& clientname);
    // send messages to a client and wait response data from it.
    bool NetMsgBusGetData(const std::string& clientname, const std::string& msgid, MsgBusParam param, 
        std::string& rsp_data, int32_t timeout_sec = 30);
    bool NetMsgBusGetData(const std::string& dest_ip, unsigned short dest_port, const std::string& msgid, MsgBusParam param, 
        std::string& rsp_data, int32_t timeout_sec = 30);

    boost::shared_ptr<NetFuture> NetMsgBusAsyncGetData(const std::string& clientname, const std::string& msgid, MsgBusParam param);
    boost::shared_ptr<NetFuture> NetMsgBusAsyncGetData(const std::string& dest_ip,
        unsigned short dest_port, const std::string& msgid, MsgBusParam param);
    // query all available services that are registered on the net message bus server
    int  NetMsgBusQueryServices(const std::string& match_str);
 
    void printAllMsgHandler(const std::string& msgid);
}


#endif
