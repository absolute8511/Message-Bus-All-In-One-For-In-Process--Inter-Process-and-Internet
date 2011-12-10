#ifndef SCOPE_GUARD_H_MYIDENTIFY_1985
#define SCOPE_GUARD_H_MYIDENTIFY_1985

#include <boost/function.hpp>
#include <boost/utility.hpp>

namespace threadpool
{
    class scope_guard:private boost::noncopyable
    {
    private:
        boost::function0<void> const m_function;
        bool m_isdisable;
    public:
        scope_guard(boost::function0<void> const &call_on_exit)
            :m_function(call_on_exit)
             ,m_isdisable(false)
        {
        }
        ~scope_guard()
        {
            if(m_isdisable || (m_function==NULL))
                return;
            m_function();
        }
        void disable()
        {
            m_isdisable = true;
        }
    };
}

#endif
