// 消息处理对象的基类模板，需要关注消息总线的类可以以该模板类为基类，这样就能
// 以简单的函数调用和相同的形式将自己的成员函数注册到消息总线
// 
#ifndef MSGBUS_HANDLER_BASE_H
#define MSGBUS_HANDLER_BASE_H
#include "threadpool.h"
#include "msgbus_interface.h"
#include "lock.hpp"
#include <map>
#include <boost/unordered_map.hpp>
#include <string>
#include <boost/bind.hpp>
#include <boost/shared_array.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace NetMsgBus
{
#define DECLARE_SP_PTR(T) typedef boost::shared_ptr<T> T##Ptr

    class MsgHandlerMgr;
    template <class T> class MsgHandler: public IMsgHandler, public boost::enable_shared_from_this< T >
    {
    protected:
        MsgHandler(){}
    public:
        friend class MsgHandlerMgr;
        typedef bool (T::*HandlerT)(const std::string&, MsgBusParam&, bool&);
        typedef bool (T::*ConstHandlerT)(const std::string&, MsgBusParam&, bool&) const;
        struct HandlerTWrapper
        {
            HandlerTWrapper()
                :type(0),
                handler_func(NULL)
            {
            }
            HandlerTWrapper(int ltype, HandlerT lhandlerfunc)
                :type(ltype),
                handler_func(lhandlerfunc)
            {
            }
            // 消息处理函数的调用类型，0代表在消息总线的同步消息处理线程中调用（和SendMsg的调用线程一致）
            // 1-代表该处理函数是一个耗时函数，因此会在放到线程池中调用
            // 2-stand for this handler can be called safely in any thread.
            // 3-代表该函数是一个UI函数，需要在UI线程中处理
            //
            int type;
            HandlerT handler_func;
        };
        // 消息处理函数集合类型
        //typedef typename std::map< std::string, HandlerTWrapper > HandlerContainerT;
        typedef typename boost::unordered_map< std::string, HandlerTWrapper > HandlerContainerT;

        // 删除该对象在消息总线上注册的所有函数
        virtual ~MsgHandler()
        {
            RemoveAllHandlers();
        }
        
        // 由消息总线调用的消息处理函数接口，对象中的所有注册过的函数都在这里进行相应的消息函数调用
        bool OnMsg(const std::string& msgid, MsgBusParam& param, bool& is_continue)
        {
            HandlerTWrapper hwrapper;
            {
                core::common::locker_guard guard(handlers_lock_);
                typename HandlerContainerT::iterator it = all_handlers_.find(msgid);
                if( it == all_handlers_.end() )
                {
                    return false;
                }

                hwrapper = it->second;
            }
            is_continue = true;
            bool result = false;
            {
                if(hwrapper.type == 0)
                {
                    if(hwrapper.handler_func != NULL)
                        result = (dynamic_cast<T*>(this)->*(hwrapper.handler_func))(msgid, param, is_continue);
                }
                else if(hwrapper.type == 1)
                {// long time function, to avoid others can not going on, we put it into threadpool.
                    result = threadpool::queue_work_task(boost::bind(hwrapper.handler_func, this->shared_from_this(), msgid, param, is_continue), 0);
                }
                else if(hwrapper.type == 2)
                {// UI event, let the ui thread to handle the function
                    // ::SendMessage(m_hWnd, WM_CALL_MYFUNC, boost::bind(it->second.handler_func, this, msgid, param, is_continue), NULL );
                }
            }
            return result;
        }
        void AddHandler(const std::string& msgid, HandlerT handler_func, int type)
        {
            HandlerTWrapper hwrapper(type, handler_func);
            {
                core::common::locker_guard guard(handlers_lock_);
                all_handlers_[msgid] = hwrapper;
            }
            RegisterMsg(msgid, this->shared_from_this(), type != 2);
        }
        void RemoveHandler(const std::string& msgid)
        {
            {
                core::common::locker_guard guard(handlers_lock_);
                all_handlers_.erase(msgid);
            }
            UnRegisterMsg(msgid, dynamic_cast<IMsgHandler*>(this));
        }
        // 从消息总线删除所有的注册消息
        void RemoveAllHandlers()
        {
            core::common::locker_guard guard(handlers_lock_);
            typename HandlerContainerT::const_iterator it = all_handlers_.begin();
            while(it != all_handlers_.end())
            {
                UnRegisterMsg(it->first, dynamic_cast<IMsgHandler*>(this));
                ++it;
            }
            all_handlers_.clear();
        }

    private:
        HandlerContainerT all_handlers_;
        core::common::locker handlers_lock_;
    };
}

#endif
