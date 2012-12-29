#include "msgbus_def.h"
#include <string>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

namespace NetMsgBus
{
int32_t padding(const void* pointer, int32_t prev_size, int32_t next_size)
{
//    int32_t leastoffset = next_size - ((unsigned long long)pointer % next_size);
//    if (leastoffset >= prev_size)
//        return leastoffset;
//    if (prev_size % next_size == 0)
//        return prev_size;
//    int32_t moreoffset = next_size - ((unsigned long long)((char *)pointer + prev_size) % next_size);
//    return prev_size + moreoffset;
    return prev_size;
}

S_ClientHostInfo::S_ClientHostInfo()
{
    ip_ = 0;
    port_ = 0;
    busy_state_ = htonl(LOW);
}
S_ClientHostInfo::S_ClientHostInfo(const std::string& ip_str, unsigned short int port)
{
    int ret = inet_pton(AF_INET, ip_str.c_str(), &ip_);
    if(ret != 1)
    {
        perror("set ip error");
        ip_ = 0;
    }
    port_ = htons(port);
    busy_state_ = htonl(LOW);
}
void S_ClientHostInfo::set_ip(const std::string& ip_str)
{
    int ret = inet_pton(AF_INET, ip_str.c_str(), &ip_);
    if (ret != 1)
    {
        perror("set ip error");
        ip_ = 0;
    }
}
void S_ClientHostInfo::set_port(unsigned short int port)
{
    port_ = htons(port);
}
void S_ClientHostInfo::set_state(kServerBusyState state)
{
    busy_state_ = htonl(state);
}
std::string S_ClientHostInfo::ip() const
{
    std::string str;
    if(ip_ == 0)
        return str;
    str.resize(INET6_ADDRSTRLEN);
    if (inet_ntop(AF_INET, &ip_, &str[0], INET6_ADDRSTRLEN) == NULL)
        perror("get ip error.");
    return str;
}

unsigned short int S_ClientHostInfo::port() const
{
    return ntohs(port_);
}
kServerBusyState S_ClientHostInfo::state() const
{
    return kServerBusyState(ntohl(busy_state_));
}

uint16_t MsgBusPackHead::PackHead(char *data, size_t len)
{
    if(len != 0 && len < Size())
        throw;
    char *p = data;
    *((uint8_t *)p) = magic;
    p += padding(p, sizeof(magic), sizeof(version));
    *((uint16_t *)p) = htons(version);
    p += padding(p, sizeof(version), sizeof(msg_type));
    *((uint8_t *)p) = msg_type;
    p += padding(p, sizeof(msg_type), sizeof(msg_id));
    *((uint32_t*)p) = htonl(msg_id);
    p += padding(p, sizeof(msg_id), sizeof(body_type));
    *((kMsgBusBodyType *)p) = (kMsgBusBodyType)htonl(body_type);
    p += padding(p, sizeof(body_type), sizeof(body_len));
    *((uint32_t*)p) = htonl(body_len);
    return (p - data);
}

int MsgBusPackHead::UnPackHead(const char *data, size_t len)
{
    if(len != 0 && len < Size())
        return -1;
    try{
    const char *p = data;
    magic = *((uint8_t *)p);
    p += padding(p, sizeof(magic), sizeof(version));
    version = ntohs(*((uint16_t *)p));
    p += padding(p, sizeof(version), sizeof(msg_type));
    msg_type = *((uint8_t *)p);
    p += padding(p, sizeof(msg_type), sizeof(msg_id));
    msg_id = ntohl(*((uint32_t*)p));
    p += padding(p, sizeof(msg_id), sizeof(body_type));
    body_type = (kMsgBusBodyType)ntohl(*((kMsgBusBodyType *)p));
    p += padding(p, sizeof(body_type), sizeof(body_len));
    body_len = ntohl(*((uint32_t*)p));
    return p-data;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusPackHead::Size()
{
    return sizeof(MsgBusPackHead);
//    return sizeof(magic) + sizeof(version) + sizeof(msg_type) +
//        sizeof(body_type) + sizeof(body_len);
//
//    要考虑结构的对齐
}

uint16_t MsgBusPackHeadReq::PackReqHead(char *data, size_t len)
{
    char *p = data;
    return PackHead(p);
}
int MsgBusPackHeadReq::UnPackReqHead(const char *data, size_t len)
{
    const char *p = data;
    int offset = UnPackHead(p);
    if( offset >= 0  && msg_type == 0)
    {
        return offset;
    }
    return -1;
}
uint32_t MsgBusPackHeadReq::Size()
{
    return MsgBusPackHead::Size();
}
uint16_t MsgBusPackHeadRsp::PackRspHead(char *data, size_t len)
{
    char *p = data;
    return PackHead(p);
}
int MsgBusPackHeadRsp::UnPackRspHead(const char *data, size_t len)
{
    const char *p = data;
    int offset = UnPackHead(p);
    if( offset >=0 && msg_type == 1 )
    {
        return offset;
    }
    return -1;
}

uint32_t MsgBusPackHeadRsp::Size()
{
    return MsgBusPackHead::Size();
}

void MsgBusRegisterReq::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadReq::Size())))
        throw;
    char *p = data;
    strncpy(p, service_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((ClientHost *)p) = service_host;
}
void MsgBusRegisterReq::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusRegisterReq::UnPackBody(const char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadReq::Size())))
        throw;
    const char *p = data;
    try{
    strncpy(service_name, p, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    service_host = *((ClientHost*)p);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusRegisterReq::UnPackData(const char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    const char *p = data;
    try{
    if ( UnPackReqHead(p) >= 0 && body_type == REQ_REGISTER )
    {
        p += MsgBusPackHeadReq::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusRegisterReq::Size()
{
    return sizeof(MsgBusRegisterReq);
//    return MsgBusPackHeadReq::Size() + MAX_SERVICE_NAME + sizeof(service_host);
}

MsgBusRegisterRsp::~MsgBusRegisterRsp()
{
    FreeVarData();
}

void MsgBusRegisterRsp::FreeVarData()
{
    if(needfree)
    {
        if(err_msg)
        {
            delete[] err_msg;
            err_msg = NULL;
        }
    }
}

void MsgBusRegisterRsp::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadRsp::Size())))
        throw;
    char *p = data;
    *((uint16_t *)p) = htons(ret_code);
    p += sizeof(ret_code);
    strncpy(p, service_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((uint16_t *)p) = htons(err_msg_len);
    p += sizeof(err_msg_len);
    strncpy(p, err_msg, err_msg_len);
}
void MsgBusRegisterRsp::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHeadRsp::Size();
    PackRspHead(p);
    p += MsgBusPackHeadRsp::Size();
    PackBody(p);
}
int MsgBusRegisterRsp::UnPackBody(const char *data, size_t len)
{
    const char *p = data;
    try{
    if( (len != 0) && (len < (sizeof(ret_code) + MAX_SERVICE_NAME + sizeof(err_msg_len))))
        return -1;
    ret_code = ntohs(*((uint16_t*)p));
    p += sizeof(ret_code);
    strncpy(service_name, p, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    err_msg_len = ntohs(*((uint16_t *)p));
    p += sizeof(err_msg_len);
    if( (len != 0) && (len < (sizeof(ret_code) + MAX_SERVICE_NAME + sizeof(err_msg_len) + err_msg_len)))
        return -1;
    FreeVarData();
    needfree = true;
    err_msg = new char[err_msg_len];
    strncpy(err_msg, p, err_msg_len);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusRegisterRsp::UnPackData(const char *data, size_t len)
{
    const char *p = data;
    try{
    if ( UnPackRspHead(p) >= 0 && body_type == RSP_REGISTER )
    {
        p += MsgBusPackHeadRsp::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusRegisterRsp::Size()
{
    return MsgBusPackHeadRsp::Size() + sizeof(ret_code) + MAX_SERVICE_NAME + sizeof(err_msg_len) + err_msg_len;
}

void MsgBusUnRegisterReq::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadReq::Size())))
        throw;
    char *p = data;
    strncpy(p, service_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((ClientHost *)p) = service_host;
}
void MsgBusUnRegisterReq::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusUnRegisterReq::UnPackBody(const char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadReq::Size())))
        throw;
    const char *p = data;
    try{
    strncpy(service_name, p, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    service_host = *((ClientHost*)p);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusUnRegisterReq::UnPackData(const char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    const char *p = data;
    try{
    if ( UnPackReqHead(p) >= 0 && body_type == REQ_UNREGISTER )
    {
        p += MsgBusPackHeadReq::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusUnRegisterReq::Size()
{
    return sizeof(MsgBusUnRegisterReq);
//    return MsgBusPackHeadReq::Size() + MAX_SERVICE_NAME + sizeof(service_host);
}


void MsgBusConfirmAliveReq::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadReq::Size())))
        throw;
    char *p = data;
    *((uint8_t*)p) = alive_flag;
}
void MsgBusConfirmAliveReq::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusConfirmAliveReq::UnPackBody(const char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadReq::Size())))
        throw;
    const char* p = data;
    try{
        alive_flag = *((uint8_t*)p);
        return 0;
    }
    catch(...)
    {
        return -1;
    }
    return 0;
}
int MsgBusConfirmAliveReq::UnPackData(const char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    const char *p = data;
    try{
    if ( UnPackReqHead(p) >= 0 && body_type == REQ_CONFIRM_ALIVE )
    {
        p += MsgBusPackHeadReq::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusConfirmAliveReq::Size()
{
    return sizeof(MsgBusConfirmAliveReq);
}

void MsgBusConfirmAliveRsp::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadRsp::Size())))
        throw;
    char *p = data;
    *((uint16_t *)p) = htons(ret_code);
}
void MsgBusConfirmAliveRsp::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHeadRsp::Size();
    PackRspHead(p);
    p += MsgBusPackHeadRsp::Size();
    PackBody(p);
}
int MsgBusConfirmAliveRsp::UnPackBody(const char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadRsp::Size())))
        throw;
    const char *p = data;
    try{
    ret_code = ntohs(*((uint16_t*)p));
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusConfirmAliveRsp::UnPackData(const char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    const char *p = data;
    try{
    if ( UnPackRspHead(p) >= 0 && body_type == RSP_REGISTER )
    {
        p += MsgBusPackHeadRsp::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusConfirmAliveRsp::Size()
{
    return MsgBusPackHeadRsp::Size() + sizeof(ret_code);
}

void MsgBusGetClientReq::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadReq::Size())))
        throw;
    char *p = data;
    strncpy(p, dest_name, MAX_SERVICE_NAME);
}
void MsgBusGetClientReq::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusGetClientReq::UnPackBody(const char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadReq::Size())))
        throw;
    const char *p = data;
    try{
    strncpy(dest_name, p, MAX_SERVICE_NAME);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusGetClientReq::UnPackData(const char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    const char *p = data;
    try{
    if ( UnPackReqHead(p) >= 0 && body_type == REQ_GETCLIENT )
    {
        p += MsgBusPackHeadReq::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusGetClientReq::Size()
{
    return sizeof(MsgBusGetClientReq);
//    return MsgBusPackHeadReq::Size() + MAX_SERVICE_NAME + sizeof(service_host);
}

void MsgBusGetClientRsp::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadRsp::Size())))
        throw;
    char *p = data;
    *((uint16_t *)p) = htons(ret_code);
    p += sizeof(ret_code);
    strncpy(p, dest_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((ClientHost *)p) = dest_host;
}
void MsgBusGetClientRsp::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHeadRsp::Size();
    PackRspHead(p);
    p += MsgBusPackHeadRsp::Size();
    PackBody(p);
}
int MsgBusGetClientRsp::UnPackBody(const char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadRsp::Size())))
        throw;
    const char *p = data;
    try{
    ret_code = ntohs(*((uint16_t*)p));
    p += sizeof(ret_code);
    strncpy(dest_name, p, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    dest_host = *((ClientHost *)p);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusGetClientRsp::UnPackData(const char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    const char *p = data;
    try{
    if ( UnPackRspHead(p) >= 0 && body_type == RSP_GETCLIENT )
    {
        p += MsgBusPackHeadRsp::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusGetClientRsp::Size()
{
    return sizeof(MsgBusGetClientRsp);
}

MsgBusSendMsgReq::~MsgBusSendMsgReq()
{
    FreeVarData();
}

void MsgBusSendMsgReq::FreeVarData()
{
    if(needfree)
    {
        if(msg_content)
        {
            delete msg_content;
            msg_content = NULL;
        }
    }
}

void MsgBusSendMsgReq::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadReq::Size())))
        throw;
    char *p = data;
    strncpy(p, dest_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    strncpy(p, from_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((uint32_t*)p) = htonl(msg_len);
    p += sizeof(msg_len);
    memcpy(p, msg_content, msg_len);
}
void MsgBusSendMsgReq::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusSendMsgReq::UnPackBody(const char *data, size_t len)
{
    const char *p = data;
    try{
    if( (len != 0) && (len < (MAX_SERVICE_NAME*2) + sizeof(msg_len) ))
        return -1;
    strncpy(dest_name, p, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    strncpy(from_name, p, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    msg_len = ntohl(*((uint32_t*)p));
    p += sizeof(msg_len);
    if( (len != 0) && (len < (MAX_SERVICE_NAME*2) + sizeof(msg_len) + msg_len))
        return -1;
    FreeVarData();
    needfree = true;
    msg_content = new char[msg_len];
    memcpy(msg_content, p, msg_len);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusSendMsgReq::UnPackData(const char *data, size_t len)
{
    const char *p = data;
    try{
    if ( UnPackReqHead(p) >= 0 && body_type == REQ_SENDMSG )
    {
        p += MsgBusPackHeadReq::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusSendMsgReq::Size()
{
//    return sizeof(MsgBusSendMsgReq);
    return MsgBusPackHeadReq::Size() + 2*MAX_SERVICE_NAME +
        sizeof(msg_len) + msg_len;
}

MsgBusSendMsgRsp::~MsgBusSendMsgRsp()
{
    FreeVarData();
}

void MsgBusSendMsgRsp::FreeVarData()
{
    if(needfree)
    {
        if(err_msg)
        {
            delete[] err_msg;
            err_msg = 0;
        }
    }
}

void MsgBusSendMsgRsp::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHeadRsp::Size())))
        throw;
    char *p = data;
    *((uint16_t *)p) = htons(ret_code);
    p += sizeof(ret_code);
    *((uint16_t*)p) = htons(err_msg_len);
    p += sizeof(err_msg_len);
    strncpy(p, err_msg, err_msg_len);
}
void MsgBusSendMsgRsp::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHeadRsp::Size();
    PackRspHead(p);
    p += MsgBusPackHeadRsp::Size();
    PackBody(p);
}
int MsgBusSendMsgRsp::UnPackBody(const char *data, size_t len)
{
    const char *p = data;
    try{
    if( (len != 0) && (len < sizeof(ret_code)))
        return -1;
    ret_code = ntohs(*((uint16_t*)p));
    p += sizeof(ret_code);
    if( (len != 0) && (len < sizeof(ret_code)))
        return -1;
    if( (len != 0) && (len < sizeof(ret_code) + sizeof(err_msg_len)))
        return -1;
    err_msg_len = ntohs(*((uint16_t*)p));
    p += sizeof(err_msg_len);
    if( (len != 0) && (len < sizeof(ret_code) + sizeof(err_msg_len) + err_msg_len))
        return -1;
    FreeVarData();
    needfree = true;
    err_msg = new char[err_msg_len];
    strncpy(err_msg, p, err_msg_len);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusSendMsgRsp::UnPackData(const char *data, size_t len)
{
    const char *p = data;
    try{
    if ( UnPackRspHead(p) >= 0 && body_type == RSP_SENDMSG )
    {
        p += MsgBusPackHeadRsp::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}
uint32_t MsgBusSendMsgRsp::Size()
{
    return MsgBusPackHeadRsp::Size() + sizeof(ret_code) +
        sizeof(err_msg_len) + err_msg_len;
}

MsgBusPackPBType::~MsgBusPackPBType()
{
    FreeVarData();
}

void MsgBusPackPBType::FreeVarData()
{
    if(needfree)
    {
        if(pbtype)
        {
            delete[] pbtype;
            pbtype = NULL;
        }
        if(pbdata)
        {
            delete[] pbdata;
            pbdata = NULL;
        }
    }
    needfree = false;
}

void MsgBusPackPBType::PackBody(char *data, size_t len)
{
    if( (len != 0) && (len < (Size() - MsgBusPackHead::Size())))
        throw;
    char *p = data;
    *((int32_t*)p) = htonl(pbtype_len);
    p += sizeof(pbtype_len);
    *((int32_t*)p) = htonl(pbdata_len);
    p += sizeof(pbdata_len);
    strncpy(p, pbtype, pbtype_len);
    p += pbtype_len;
    memcpy(p, pbdata, pbdata_len);
}

void MsgBusPackPBType::PackData(char *data, size_t len)
{
    if( (len != 0) && (len < Size()))
        throw;
    char *p = data;
    body_len = Size() - MsgBusPackHead::Size();
    PackHead(p);
    p += MsgBusPackHead::Size();
    PackBody(p);
}

int MsgBusPackPBType::UnPackBody(const char *data, size_t len)
{
    if( (len != 0) && (len < sizeof(pbtype_len)) )
        throw;
    const char *p = data;
    try{
    pbtype_len = ntohl(*((int32_t*)p));
    p += sizeof(pbtype_len);
    if( (len != 0) && (len < sizeof(pbtype_len) + pbtype_len) )
        return -1;
    pbdata_len = ntohl(*((int32_t*)p));
    if( (len != 0) && (len < (Size() - MsgBusPackHead::Size())))
        return -1;
    p += sizeof(pbdata_len);

    FreeVarData();
    needfree = true;

    pbtype = new char[pbtype_len];
    strncpy(pbtype, p, pbtype_len);
    p += pbtype_len;
    if( (len != 0) && (len < sizeof(pbtype_len) + pbtype_len + sizeof(pbdata_len)) )
        return -1;
    pbdata = new char[pbdata_len];
    memcpy(pbdata, p, pbdata_len);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}

int MsgBusPackPBType::UnPackData(const char *data, size_t len)
{
    const char *p = data;
    try{
    if ( UnPackHead(p) >= 0 && body_type == BODY_PBTYPE )
    {
        p += MsgBusPackHead::Size();
        return UnPackBody(p);
    }
    return -1;
    }
    catch(...)
    {
        return -1;
    }
}

uint32_t MsgBusPackPBType::Size()
{
    return MsgBusPackHead::Size() + sizeof(pbtype_len) + pbtype_len + 
        sizeof(pbdata_len) + pbdata_len;
}


}
