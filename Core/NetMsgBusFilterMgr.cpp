#include "NetMsgBusFilterMgr.h"
#include "SimpleLogger.h"
#include <vector>
#include <stdio.h>

using namespace core;

namespace NetMsgBus
{
static LoggerCategory g_log("FilterMgr");

static std::vector<std::string> m_include_senders;
static std::vector<std::string> m_exclude_msgids;

void FilterMgr::AddIncludeSender(const std::string& sender_name)
{
    m_include_senders.push_back(sender_name);
}

void FilterMgr::AddExcludeMsgId(const std::string& msgid)
{
    m_exclude_msgids.push_back(msgid);
}

bool FilterMgr::FilterBySender(const std::string& name)
{
    // no filter set.
    if(m_include_senders.empty())
        return true;
    for(size_t i = 0; i < m_include_senders.size(); i++)
    {
        if(m_include_senders[i] == name)
            return true;
    }
    g_log.Log(lv_warn, "the sender : %s is not included in config to allow send message to me.", name.c_str());
    return false;
}

bool FilterMgr::FilterByMsgId(const std::string& msgid)
{
    if(m_exclude_msgids.empty())
        return true;
    for(size_t i = 0; i < m_exclude_msgids.size(); i++)
    {
        if(msgid.find(m_exclude_msgids[i]) == 0)
        {
            g_log.Log(lv_warn, "interprocess message filter by config. %s.", msgid.c_str());
            return false;
        }
    }
    return true;
}

}
