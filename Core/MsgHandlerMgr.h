#ifndef  CORE_MSGHANDLERMGR_H
#define  CORE_MSGHANDLERMGR_H
#include "msgbus_interface.h"
#include "lock.hpp"

namespace NetMsgBus{

class MsgHandlerMgr
{
public:
    template<typename T> static void GetInstance( boost::shared_ptr<T>& sp, bool registerhandler = true)
    {
        const std::string& classname = T::ClassName();
        core::common::locker_guard guard(m_locker);
        MsgHandlerContainerT::iterator it = m_all_handlers.find(classname);
        bool needreg = false;
        if(it == m_all_handlers.end())
        {
            boost::shared_ptr<IMsgHandler> newobj(new T());
            it = m_all_handlers.insert(m_all_handlers.begin(), std::make_pair(classname, newobj));
            needreg = true;
        }
        else if(it->second == NULL)
        {
            it->second = boost::shared_ptr<IMsgHandler>(new T());
            needreg = true;
        }
        boost::shared_ptr<T> realsp = boost::dynamic_pointer_cast< T >(it->second);
        if(realsp && registerhandler && needreg)
        {
            realsp->InitMsgHandler();
        }
        sp.swap(realsp);
    }
    template<typename T> static void CreateInstance(boost::shared_ptr<T>& sp,  bool registerhandler = true)
    {
        sp.reset(new T());
        if(registerhandler)
        {
            sp->InitMsgHandler();
        }
    }
    // note: do not call any GetInstance/DropInstance/DropAllInstance in the destructor of the class 
    // which was once created by GetInstance.
    static void DropInstance(const std::string& classname)
    {
        core::common::locker_guard guard(m_locker);
        m_all_handlers.erase(classname);
    }
    static void DropAllInstance()
    {
        core::common::locker_guard guard(m_locker);
        m_all_handlers.clear();
    }

private:
    static core::common::locker  m_locker;
    typedef std::map<std::string, boost::shared_ptr<IMsgHandler> > MsgHandlerContainerT;
    static MsgHandlerContainerT m_all_handlers;
};

}
#endif
