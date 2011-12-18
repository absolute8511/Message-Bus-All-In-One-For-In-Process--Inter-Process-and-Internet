#ifndef NETMSGBUS_UTILITY_H
#define NETMSGBUS_UTILITY_H

#include "msgbus_interface.h"
#include "TcpSock.h"
#include "NetMsgBusFilterMgr.h"
#include "CommonUtility.hpp"
#include <string>
#include <stdio.h>
#include <boost/shared_array.hpp>
#include <fcntl.h>

using namespace core::net;

namespace NetMsgBus
{
inline void EncodeMsgKeyValue(std::string& orig_value)
{
    // replace % and &
    size_t i = 0; 
    while(i < orig_value.size())
    {
        if(orig_value[i] == '%')
        {
            orig_value.replace(i, 1, "%25");
            i += 3;
        }
        else if(orig_value[i] == '&')
        {
            orig_value.replace(i, 1, "%26");
            i += 3;
        }
        else
        {
            ++i;
        }
    }
}

inline void DecodeMsgKeyValue(std::string& orig_value)
{
    // replace % 
    size_t i = 0; 
    while(i < orig_value.size())
    {
        if(orig_value[i] == '%')
        {
            if(orig_value[i+1]=='2' && orig_value[i+2] == '5')
                orig_value.replace(i, 3, "%");
            else if(orig_value[i+1]=='2' && orig_value[i+2] == '6')
                orig_value.replace(i, 3, "&");
            else
            {
                printf("warning: unknow encode msgbus value string : %s.", orig_value.substr(i, 3).c_str());
                orig_value.replace(i, 3, "");
            }
        }
        ++i;
    }
}

inline bool GetMsgKey(const std::string& netmsgbus_msgcontent, const std::string& msgkey, 
    std::string& msgvalue)
{
    std::size_t startpos = netmsgbus_msgcontent.find(msgkey + "=");
    if(startpos != std::string::npos)
    {
        std::size_t endpos = netmsgbus_msgcontent.find("&", startpos);
        if(endpos != std::string::npos)
        {
            msgvalue = std::string(netmsgbus_msgcontent, startpos + msgkey.size() + 1,
                endpos - startpos - (msgkey.size() + 1));
        }
        else
        {
            msgvalue = std::string(netmsgbus_msgcontent, startpos + msgkey.size() + 1);
        }
        DecodeMsgKeyValue(msgvalue);
        return true;
    }
    //printf("netmsgbus_msgcontent error, no %s found.\n", msgkey.c_str());
    return false;
}

inline bool CheckMsgSender(const std::string& netmsgbus_msgcontent, std::string& msgsender)
{
    if(GetMsgKey(netmsgbus_msgcontent, "msgsender", msgsender))
    {
        return FilterMgr::FilterBySender(msgsender);
    }
    return false;
}

inline bool CheckMsgId(const std::string& netmsgbus_msgcontent, std::string& msgid)
{
    if(GetMsgKey(netmsgbus_msgcontent, "msgid", msgid))
    {
        return FilterMgr::FilterByMsgId(msgid);
    }
    return false;
}

inline bool GetMsgParam(const std::string& netmsgbus_msgcontent, boost::shared_array<char>& msgparam, uint32_t& param_len)
{
    std::string msgparamstr;
    if(GetMsgKey(netmsgbus_msgcontent, "msgparam", msgparamstr))
    {
        assert(msgparamstr.size());
        msgparam.reset(new char[msgparamstr.size()]);
        memcpy(msgparam.get(), msgparamstr.data(), msgparamstr.size());
        param_len = msgparamstr.size();
        return true;
    }
    printf("netmsgbus_msgcontent error, no msgparam found.\n");
    return false;
}

// response to the client who has send a msg using sync mode.
inline void NetMsgBusRspSendMsg(TcpSockSmartPtr sp_tcp, const std::string& netmsgbus_msgcontent)
{
    //printf("process a sync request :%lld\n", (int64_t)core::utility::GetTickCount());
    std::string msgid;
    if(CheckMsgId(netmsgbus_msgcontent, msgid))
    {
        boost::shared_array<char> data;
        uint32_t data_len;
        if(GetMsgParam(netmsgbus_msgcontent, data, data_len))
        {
            SendMsg(msgid, data, data_len);
            //printf("local process the sync req finished on fd:%d, ready to send back to sender.\n", sp_tcp->GetFD());
            // when finished process, write the data back to the client.
            sp_tcp->SendData((char*)&data_len, sizeof(data_len));
            sp_tcp->SendData(data.get(), data_len);
            //printf("process a sync request finished :%lld\n", (int64_t)core::utility::GetTickCount());
        }
    }
}

inline void NetMsgBusToLocalMsgBus(const std::string& netmsgbus_msgcontent)
{
    std::string msgid;
    if(CheckMsgId(netmsgbus_msgcontent, msgid))
    {
        boost::shared_array<char> msgparam;
        uint32_t param_len;
        if(GetMsgParam(netmsgbus_msgcontent, msgparam, param_len))
        {
            PostMsg(msgid, msgparam, param_len);
        }
    }
}
}
#endif // end of NETMSGBUS_UTILITY_H