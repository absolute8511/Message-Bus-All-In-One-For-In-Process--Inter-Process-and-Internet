#include "MsgHandlerMgr.h"

namespace NetMsgBus
{
core::common::locker MsgHandlerMgr::m_locker;
MsgHandlerMgr::MsgHandlerContainerT MsgHandlerMgr::m_all_handlers;

}
