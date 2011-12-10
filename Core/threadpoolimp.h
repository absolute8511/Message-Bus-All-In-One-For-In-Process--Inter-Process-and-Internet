#ifndef THREADPOOLIMP_H_MYIDENTIFY_1985
#define THREADPOOLIMP_H_MYIDENTIFY_1985

#include "lock.hpp"
#include "condition.hpp"
#include "multitimer.h"
#include <pthread.h>
#include <signal.h>
#include <deque>
#include <vector>
#include <stdio.h>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <map>
//the max timers can exist at the same time. 
#ifndef THREAD_POOL_MAX_TIMER
#define THREAD_POOL_MAX_TIMER 16
#endif

namespace threadpool
{
    class named_worker_thread;
    typedef boost::function<void()> task_type;
    class threadpoolimp
    {
    public:
        threadpoolimp()
            :m_terminateall(false),
            m_timertask_container(THREAD_POOL_MAX_TIMER)
        {
        }
        ~threadpoolimp()
        {
        }

        void terminateall()
        {
            core::common::locker_guard guard(m_locker);
            m_terminateall = true;
            m_task_container.clear();
            m_timertask_container.clear();
            notifyall_task_coming_event();
        }
        inline bool getterminate()
        {
            return m_terminateall;
        }
        void push_task(const task_type& task)
        {
            core::common::locker_guard guard(m_locker);
            m_task_container.push_back(task);
            notifyone_task_coming_event();
        }
        // the caller must make sure thread safe
        task_type& top_task()
        {
            return m_task_container.front();
        }
        // the caller must make sure thread safe
        void pop_task()
        {
            m_task_container.pop_front();
        }
        // the caller must make sure thread safe
        inline bool taskempty()
        {
            return m_task_container.empty();
        }
        void notifyone_task_coming_event()
        {
            m_task_coming_event.notify_one();
        }
        void notifyall_task_coming_event()
        {
            m_task_coming_event.notify_all();
        }
        void wait_task_coming_event()
        {
            m_task_coming_event.wait(m_locker);
        }
        void push_thread(pthread_t pid);
        bool threadempty();
        void push_alonethread(pthread_t pid);
        void erase_alonethread(pthread_t pid);
        void waittoterminate();

        // caller must make sure thread safe.
        void add_named_thread(pthread_t pid, const std::string& threadname, boost::shared_ptr<named_worker_thread> spworker);
        bool push_task_to_named_thread(const task_type& task, const std::string& threadname);
         // 仅仅删除线程对象的引用
        void remove_named_thread(const std::string& threadname);

        volatile bool m_terminateall;
        core::common::locker m_locker;
        //condition m_worker_idle_event;
        core::common::condition m_task_coming_event;
        std::deque<task_type> m_task_container;
        std::vector<pthread_t> m_pids;
        // the threads for long run.
        std::vector<pthread_t> m_alone_pids;
        std::vector<multitimer> m_timertask_container;
        typedef std::map< std::string, std::pair< boost::shared_ptr<named_worker_thread>, pthread_t > > NamedThreadContainerT;
        NamedThreadContainerT m_named_threads;
    };
}

#endif
