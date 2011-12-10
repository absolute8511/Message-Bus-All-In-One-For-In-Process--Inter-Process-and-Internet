#ifndef named_worker_thread_H_MYIDENTIFY_1985
#define named_worker_thread_H_MYIDENTIFY_1985
#include "scope_guard.hpp"
#include "lock.hpp"
#include "condition.hpp"
#include "multitimer.h"
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <deque>

namespace threadpool
{
    class threadpoolimp;
    class named_worker_thread : public boost::enable_shared_from_this<named_worker_thread>
    {
    private:
        std::string m_threadname;
        threadpoolimp* m_tpool;
        volatile bool m_terminate;
        core::common::locker m_locker;
        core::common::condition m_task_coming_event;
        std::deque<task_type> m_task_container;

        void remove_self_from_threadpool();
        bool execute_task();
        void run();
        //if thread died unexpected,we should recreate it.
        void died_unexpect();
        static void* threadproc(void* param);

    public:
        named_worker_thread(const std::string& threadname, threadpoolimp* ptpool);
        void terminate_thread();
        bool queue_task(const task_type& task);
        static bool create_and_attach(threadpoolimp* pool, const std::string& threadname);
    };

}

#endif
