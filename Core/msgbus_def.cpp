#include "msgbus_def.h"
#include <string>
#include <string.h>
#include <stdio.h>

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

uint16_t MsgBusPackHead::PackHead(char *data)
{
    char *p = data;
    *((uint8_t *)p) = magic;
    p += padding(p, sizeof(magic), sizeof(version));
    *((uint16_t *)p) = version;
    p += padding(p, sizeof(version), sizeof(msg_type));
    *((uint8_t *)p) = msg_type;
    p += padding(p, sizeof(msg_type), sizeof(body_type));
    *((kMsgBusBodyType *)p) = body_type;
    p += padding(p, sizeof(body_type), sizeof(body_len));
    *((uint32_t*)p) = body_len;
    return (p - data);
}

int MsgBusPackHead::UnPackHead(const char * data)
{
    try{
    const char *p = data;
    magic = *((uint8_t *)p);
    p += padding(p, sizeof(magic), sizeof(version));
    version = *((uint16_t *)p);
    p += padding(p, sizeof(version), sizeof(msg_type));
    msg_type = *((uint8_t *)p);
    p += padding(p, sizeof(msg_type), sizeof(body_type));
    body_type = *((kMsgBusBodyType *)p);
    p += padding(p, sizeof(body_type), sizeof(body_len));
    body_len = *((uint32_t*)p);
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

uint16_t MsgBusPackHeadReq::PackReqHead(char *data)
{
    char *p = data;
    return PackHead(p);
}
int MsgBusPackHeadReq::UnPackReqHead(const char *data)
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
uint16_t MsgBusPackHeadRsp::PackRspHead(char *data)
{
    char *p = data;
    return PackHead(p);
}
int MsgBusPackHeadRsp::UnPackRspHead(const char *data)
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

void MsgBusRegisterReq::PackBody(char *data)
{
    char *p = data;
    strncpy(p, service_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((ClientHost *)p) = service_host;
}
void MsgBusRegisterReq::PackData(char *data)
{
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusRegisterReq::UnPackBody(const char *data)
{
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
int MsgBusRegisterReq::UnPackData(const char *data)
{
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

void MsgBusRegisterRsp::PackBody(char *data)
{
    char *p = data;
    *((uint16_t *)p) = ret_code;
    p += sizeof(ret_code);
    strncpy(p, service_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((uint16_t *)p) = err_msg_len;
    p += sizeof(err_msg_len);
    strncpy(p, err_msg, err_msg_len);
}
void MsgBusRegisterRsp::PackData(char *data)
{
    char *p = data;
    body_len = Size() - MsgBusPackHeadRsp::Size();
    PackRspHead(p);
    p += MsgBusPackHeadRsp::Size();
    PackBody(p);
}
int MsgBusRegisterRsp::UnPackBody(const char *data)
{
    const char *p = data;
    try{
    ret_code = *((uint16_t*)p);
    p += sizeof(ret_code);
    strncpy(service_name, p, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    err_msg_len = *((uint16_t *)p);
    p += sizeof(err_msg_len);
    strncpy(err_msg, p, err_msg_len);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusRegisterRsp::UnPackData(const char *data)
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

void MsgBusUnRegisterReq::PackBody(char *data)
{
    char *p = data;
    strncpy(p, service_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((ClientHost *)p) = service_host;
}
void MsgBusUnRegisterReq::PackData(char *data)
{
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusUnRegisterReq::UnPackBody(const char *data)
{
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
int MsgBusUnRegisterReq::UnPackData(const char *data)
{
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


void MsgBusConfirmAliveReq::PackBody(char *data)
{
    char *p = data;
    *((uint8_t*)p) = alive_flag;
}
void MsgBusConfirmAliveReq::PackData(char *data)
{
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusConfirmAliveReq::UnPackBody(const char *data)
{
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
int MsgBusConfirmAliveReq::UnPackData(const char *data)
{
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

void MsgBusConfirmAliveRsp::PackBody(char *data)
{
    char *p = data;
    *((uint16_t *)p) = ret_code;
}
void MsgBusConfirmAliveRsp::PackData(char *data)
{
    char *p = data;
    body_len = Size() - MsgBusPackHeadRsp::Size();
    PackRspHead(p);
    p += MsgBusPackHeadRsp::Size();
    PackBody(p);
}
int MsgBusConfirmAliveRsp::UnPackBody(const char *data)
{
    const char *p = data;
    try{
    ret_code = *((uint16_t*)p);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusConfirmAliveRsp::UnPackData(const char *data)
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
uint32_t MsgBusConfirmAliveRsp::Size()
{
    return MsgBusPackHeadRsp::Size() + sizeof(ret_code);
}

void MsgBusGetClientReq::PackBody(char *data)
{
    char *p = data;
    strncpy(p, dest_name, MAX_SERVICE_NAME);
}
void MsgBusGetClientReq::PackData(char *data)
{
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusGetClientReq::UnPackBody(const char *data)
{
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
int MsgBusGetClientReq::UnPackData(const char *data)
{
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

void MsgBusGetClientRsp::PackBody(char *data)
{
    char *p = data;
    *((uint16_t *)p) = ret_code;
    p += sizeof(ret_code);
    strncpy(p, dest_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((ClientHost *)p) = dest_host;
}
void MsgBusGetClientRsp::PackData(char *data)
{
    char *p = data;
    body_len = Size() - MsgBusPackHeadRsp::Size();
    PackRspHead(p);
    p += MsgBusPackHeadRsp::Size();
    PackBody(p);
}
int MsgBusGetClientRsp::UnPackBody(const char *data)
{
    const char *p = data;
    try{
    ret_code = *((uint16_t*)p);
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
int MsgBusGetClientRsp::UnPackData(const char *data)
{
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

void MsgBusSendMsgReq::PackBody(char *data)
{
    char *p = data;
    strncpy(p, dest_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    strncpy(p, from_name, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    *((uint32_t*)p) = msg_id;
    p += sizeof(msg_id);
    *((uint32_t*)p) = msg_len;
    p += sizeof(msg_len);
    memcpy(p, msg_content, msg_len);
}
void MsgBusSendMsgReq::PackData(char *data)
{
    char *p = data;
    body_len = Size() - MsgBusPackHeadReq::Size();
    PackReqHead(p);
    p += MsgBusPackHeadReq::Size();
    PackBody(p);
}
int MsgBusSendMsgReq::UnPackBody(const char *data)
{
    const char *p = data;
    try{
    strncpy(dest_name, p, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    strncpy(from_name, p, MAX_SERVICE_NAME);
    p += MAX_SERVICE_NAME;
    msg_id = *((uint32_t*)p);
    p += sizeof(msg_id);
    msg_len = *((uint32_t*)p);
    p += sizeof(msg_len);
    //printf("sendmsg msg_len = %u,\n", msg_len);
    memcpy(msg_content, p, msg_len);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusSendMsgReq::UnPackData(const char *data)
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
    return MsgBusPackHeadReq::Size() + 2*MAX_SERVICE_NAME + sizeof(msg_id) + 
        sizeof(msg_len) + msg_len;
}

void MsgBusSendMsgRsp::PackBody(char *data)
{
    char *p = data;
    *((uint16_t *)p) = ret_code;
    p += sizeof(ret_code);
    *((uint32_t*)p) = msg_id;
    p += sizeof(msg_id);
    *((uint32_t*)p) = err_msg_len;
    p += sizeof(err_msg_len);
    strncpy(p, err_msg, err_msg_len);
}
void MsgBusSendMsgRsp::PackData(char *data)
{
    char *p = data;
    body_len = Size() - MsgBusPackHeadRsp::Size();
    PackRspHead(p);
    p += MsgBusPackHeadRsp::Size();
    PackBody(p);
}
int MsgBusSendMsgRsp::UnPackBody(const char *data)
{
    const char *p = data;
    try{
    ret_code = *((uint16_t*)p);
    p += sizeof(ret_code);
    msg_id = *((uint32_t*)p);
    p += sizeof(msg_id);
    err_msg_len = *((uint32_t*)p);
    p += sizeof(err_msg_len);
    strncpy(err_msg, p, err_msg_len);
    return 0;
    }
    catch(...)
    {
        return -1;
    }
}
int MsgBusSendMsgRsp::UnPackData(const char *data)
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
    return MsgBusPackHeadRsp::Size() + sizeof(ret_code) + sizeof(msg_id) + 
        sizeof(err_msg_len) + err_msg_len;
}


}
