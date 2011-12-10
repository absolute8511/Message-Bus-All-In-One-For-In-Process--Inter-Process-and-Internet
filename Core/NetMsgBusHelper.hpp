#ifndef  CORE_NETMSGBUS_HELPER_H
#define  CORE_NETMSGBUS_HELPER_H

#include "../Core/msgbus_interface.h"
#include "../Core/msgbus_handlerbase.hpp"
#include "../Core/condition.hpp"
#include "MsgHandlerMgr.h"
#include <string>
#include <stdio.h>

using namespace NetMsgBus;


class RegisterReceiverHandler : public MsgHandler<RegisterReceiverHandler>
{
public:
    friend class NetMsgBus::MsgHandlerMgr;
    static std::string ClassName()
    {
        return "RegisterReceiverHandler";
    }
    void InitMsgHandler()
    {
        AddHandler("netmsgbus.server.regreceiver.success", &RegisterReceiverHandler::OnRegisterReceiverRsp, 0);
        AddHandler("netmsgbus.server.regreceiver.failed", &RegisterReceiverHandler::OnRegisterReceiverRsp, 0);
        m_reg_status = false;
    }

    bool OnRegisterReceiverRsp(const std::string& msgid, MsgBusParam& param, bool& is_continue)
    {
        std::string regname(param.paramdata.get(), param.paramlen);
        //printf("register receiver rsp : %s.\n", regname.c_str());
        if(msgid == "netmsgbus.server.regreceiver.success")
        {
            m_reg_status = true;
        }
        else
        {
            m_reg_status = false;
        }
        m_reg_event.notify_one();
        return true;
    }
    bool WaitForRsp()
    {
        core::common::locker_guard guard(m_lock);
        m_reg_event.wait(m_lock);
        return m_reg_status;
    }
private:
    bool m_reg_status;
    core::common::condition m_reg_event;
    core::common::locker    m_lock;
};

DECLARE_SP_PTR(RegisterReceiverHandler);
#endif
