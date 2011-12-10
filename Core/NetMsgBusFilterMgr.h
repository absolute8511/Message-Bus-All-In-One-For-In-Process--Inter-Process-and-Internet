#ifndef CORE_NETMSGBUS_FILTERMGR_H
#define CORE_NETMSGBUS_FILTERMGR_H

#include <string>

namespace NetMsgBus
{
// use for filting some interprocess message for safe situation
class FilterMgr
{
public:
    // using prefix of sender name compare for the sender who allow send message to me.
    static void AddIncludeSender(const std::string& sender_name);
    // using prefix of message id to filter the messages from the other process.
    static void AddExcludeMsgId(const std::string& msgid);
    static bool FilterBySender(const std::string& name);
    static bool FilterByMsgId(const std::string& msgid);
};

}

#endif
