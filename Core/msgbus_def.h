// 消息总线协议定义，定义了消息总线服务器和消息总线消息收发客户端之间的通信方式
// 每个客户端可以向服务器注册一个服务代表本客户端提供某个特定的服务
// 服务名代表服务内容，因此服务名相同的主机应该严格提供对外一致的服务行为，服务名相同的各个主机相当于提供了负载均衡和集群功能
//
#ifndef CORE_MSGBUS_DEF_H
#define CORE_MSGBUS_DEF_H
#include <stdint.h>
#include <sys/types.h>
#define MAX_SERVICE_NAME 64

namespace NetMsgBus
{

#pragma pack(push,1)
enum kServerBusyState {
    LOW = 0,      // 4/9 ratio to be chosen 
    MIDDLE,       // 1/3 ratio to be chosen
    HIGH,         // 2/9 ratio to be chosen
    UNAVAILABLE,
};

enum kMsgBusBodyType {
    REQ_REGISTER                         = 0x010001,      // 向消息总线中心服务器注册消息服务
    REQ_UNREGISTER                       = 0x010002,
    REQ_CONFIRM_ALIVE                    = 0x010003,
    REQ_SENDMSG                          = 0x010004,      // 通过消息中转服务器发送消息
    // （注：是否需要确认收到的消息由接收方和发送方互相协定，如在消息参数的内容里面加字段来表明是否需要回发确认消息）
    REQ_GETCLIENT                        = 0x010005,      // 获取客户端的地址信息

    RSP_REGISTER                         = 0x020001,
    RSP_UNREGISTER                       = 0x020002,
    RSP_CONFIRM_ALIVE                    = 0x020003,
    RSP_SENDMSG                          = 0x020004,
    RSP_GETCLIENT                        = 0x020005,

    BODY_PBTYPE                          = 0x030001,    // body is the protocol buffer data.
    BODY_JSONTYPE                        = 0x030002,    // body is the json string.
};

typedef struct S_ClientHostInfo {
    S_ClientHostInfo(uint32_t ip, unsigned short int port)
        :server_ip(ip),
        server_port(port),
        busy_state(LOW)
    {
    }
    S_ClientHostInfo()
        :server_ip(0),
        server_port(0),
        busy_state(LOW)
    {
    }
    uint32_t  server_ip;
    unsigned short int server_port;
    kServerBusyState  busy_state;
} ClientHost;

class MsgBusPackHead 
{
public:
    MsgBusPackHead()
        :magic(0x66),
        version(0x0001),
        body_len(0)
    {
    }
    MsgBusPackHead(uint8_t type, kMsgBusBodyType msgbody_type)
        :magic(0x66),
        version(0x0001),
        msg_type(type),
        body_type(msgbody_type),
        body_len(0)
    {
    }

    uint16_t PackHead(char *data, size_t len = 0);
    int  UnPackHead(const char *data, size_t len = 0);
    uint32_t Size();
    uint8_t             magic;
    uint16_t            version;
    uint8_t             msg_type;   // 0: request, 1: response, 2: notify
    kMsgBusBodyType     body_type;
    uint32_t            body_len;
};
class MsgBusPackHeadReq : public MsgBusPackHead
{
public:
    MsgBusPackHeadReq(kMsgBusBodyType type)
        :MsgBusPackHead(0, type)
    {
    }

    uint16_t PackReqHead(char *data, size_t len = 0);
    int  UnPackReqHead(const char *data, size_t len = 0);
    uint32_t Size();
};

class MsgBusPackHeadRsp : public MsgBusPackHead
{
public:
    MsgBusPackHeadRsp(kMsgBusBodyType type)
        :MsgBusPackHead(1, type)
    {
    }
    uint16_t PackRspHead(char *data, size_t len = 0);
    int  UnPackRspHead(const char *data, size_t len = 0);
    uint32_t Size();
};

class MsgBusRegisterReq : public MsgBusPackHeadReq
{
public:
    MsgBusRegisterReq()
        :MsgBusPackHeadReq(REQ_REGISTER)
    {
    }

private:
    MsgBusRegisterReq(MsgBusRegisterReq& src);

public:
    void PackBody(char *data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();

    char service_name[MAX_SERVICE_NAME];
    ClientHost service_host;
};

class MsgBusRegisterRsp : public MsgBusPackHeadRsp
{
public:
    MsgBusRegisterRsp()
        :MsgBusPackHeadRsp(RSP_REGISTER),
        err_msg_len(0),
        err_msg(0),
        needfree(false)
    {
    }

    ~MsgBusRegisterRsp();
private:
    MsgBusRegisterRsp(const MsgBusRegisterRsp&);
public:
    void PackBody(char *data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();
    const char* GetErrMsg() const
    {
        return err_msg;
    }
    void SetVarData(char* var_err_msg)
    {
        FreeVarData();
        err_msg = var_err_msg;
        needfree = false;
    }
    void FreeVarData();
    uint16_t ret_code;
    char   service_name[MAX_SERVICE_NAME];
    uint16_t err_msg_len;
private:
    char * err_msg;
    bool  needfree;
};

class MsgBusUnRegisterReq : public MsgBusPackHeadReq
{
public:
    MsgBusUnRegisterReq()
        :MsgBusPackHeadReq(REQ_UNREGISTER)
    {
    }

private:
    MsgBusUnRegisterReq(MsgBusUnRegisterReq& src);

public:
    void PackBody(char *data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();

    char service_name[MAX_SERVICE_NAME];
    ClientHost service_host;
};

class MsgBusConfirmAliveReq : public MsgBusPackHeadReq
{
public:
    MsgBusConfirmAliveReq()
        :MsgBusPackHeadReq(REQ_CONFIRM_ALIVE),
        alive_flag(0)
    {
    }
    void PackBody(char *data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();
    uint8_t  alive_flag;
};

class MsgBusConfirmAliveRsp : public MsgBusPackHeadRsp
{
public:
    MsgBusConfirmAliveRsp()
        :MsgBusPackHeadRsp(RSP_CONFIRM_ALIVE),
        ret_code(0)
    {
    }
    void PackBody(char *data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();
    uint16_t ret_code;
};
// 请求向某个客户端发送消息，此协议仅用于更新特定客户端的地址信息
// 得到更新后的信息，由请求者自己向新地址发送具体的消息内容，消息格式
// 由客户端之间进行协商

class MsgBusGetClientReq : public MsgBusPackHeadReq 
{
public:
    MsgBusGetClientReq()
        :MsgBusPackHeadReq(REQ_GETCLIENT)
    {
    }

    void PackBody(char *data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();

    char dest_name[MAX_SERVICE_NAME];
};
class MsgBusGetClientRsp : public MsgBusPackHeadRsp
{
public:
    MsgBusGetClientRsp()
        :MsgBusPackHeadRsp(RSP_GETCLIENT)
    {
    }

    void PackBody(char * data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();

    uint16_t ret_code;
    char dest_name[MAX_SERVICE_NAME];
    ClientHost dest_host; 
};

// 此协议用于通过消息中心转发消息，当两个客户端无法直接通信时可以使用此协议进行通信
//
class MsgBusSendMsgReq : public MsgBusPackHeadReq 
{
public:
    MsgBusSendMsgReq()
        :MsgBusPackHeadReq(REQ_SENDMSG),
        msg_len(0),
        msg_content(0),
        needfree(false)
    {
    }

    ~MsgBusSendMsgReq();
    void PackBody(char *data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();
    const char* GetMsgContent() const
    {
        return msg_content;
    }
    void SetVarData(char* var_msg_content)
    {
        FreeVarData();
        msg_content = var_msg_content;
        needfree = false;
    }
    void FreeVarData();

    char dest_name[MAX_SERVICE_NAME];
    char from_name[MAX_SERVICE_NAME];
    uint32_t msg_id;
    uint32_t msg_len;
private:
    char * msg_content;
    bool needfree;
};

class MsgBusSendMsgRsp : public MsgBusPackHeadRsp
{
public:
    MsgBusSendMsgRsp()
        :MsgBusPackHeadRsp(RSP_SENDMSG),
        err_msg_len(0),
        err_msg(0),
        needfree(false)
    {
    }

    ~MsgBusSendMsgRsp();
    void PackBody(char * data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();
    const char* GetErrMsg() const
    {
        return err_msg;
    }
    void SetVarData(char* var_err_msg)
    {
        FreeVarData();
        err_msg = var_err_msg;
        needfree = false;
    }
    void FreeVarData();

    uint16_t ret_code;
    uint32_t msg_id;
    uint16_t err_msg_len;
private:
    char * err_msg;
    bool needfree;
};

class MsgBusPackPBType:public MsgBusPackHead
{
public:
    MsgBusPackPBType()
        :MsgBusPackHead(0, BODY_PBTYPE),
        pbtype(0),
        pbdata(0),
        needfree(false)
    {
    }
    ~MsgBusPackPBType();
    void PackBody(char * data, size_t len = 0);
    void PackData(char *data, size_t len = 0);
    int UnPackBody(const char *data, size_t len = 0);
    int UnPackData(const char *data, size_t len = 0);
    uint32_t Size();
    void SetVarData(char* var_pbtype, char* var_pbdata)
    {
        FreeVarData();
        pbtype = var_pbtype;
        pbdata = var_pbdata;
        needfree = false;
    }
    const char* GetPBType() const
    {
        return pbtype;
    }
    const char* GetPBData() const
    {
        return pbdata;
    }
    void FreeVarData();

    int32_t   pbtype_len;
    int32_t   pbdata_len;
private:
    char*     pbtype;
    char*     pbdata;
    bool  needfree;
};


#pragma pack(pop)

struct ClientHostIsEqual
{
    bool operator()(const ClientHost& left,const ClientHost& right)
    {
       return (left.server_ip == right.server_ip) && 
             (left.server_port == right.server_port);
    }
};

}
#endif  // CORE_MSGBUS_DEF_H
