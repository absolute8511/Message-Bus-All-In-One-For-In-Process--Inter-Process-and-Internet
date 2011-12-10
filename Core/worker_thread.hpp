#ifndef WORKER_THREAD_H_MYIDENTIFY_1985
#define WORKER_THREAD_H_MYIDENTIFY_1985
#include "scope_guard.hpp"
#include "threadpoolimp.h"
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>

namespace threadpool
{
    class worker_thread
    {
    private:
        threadpoolimp* m_tpool;
    public:
        worker_thread(threadpoolimp* pool)
            :m_tpool(pool)
        {
        }
        void run()
        {
            scope_guard notify_exception(boost::bind(&worker_thread::died_unexpect,this));
            while(execute_task()) {}
            notify_exception.disable();
            //printf("thread end in normal way.\n");
        }
        //if thread died unexpected,we should recreate it.
        void died_unexpect()
        {
            printf("thread died unexpect.\n");
            create_and_attach(m_tpool);
        }
        bool execute_task()
        {
            task_type task;
            {
                //printf("starting to get the task.\n");
                core::common::locker_guard lg(m_tpool->m_locker);

                if(m_tpool->getterminate())
                {
                    return false;
                }
                while(m_tpool->taskempty())
                {
                    if(m_tpool->getterminate())
                    {
                        return false;
                    }
                    else
                    {
                        //m_tpool->notifyall_worker_idle_event();
                        //printf("wait the task.\n");
                        m_tpool->wait_task_coming_event();
                        //printf("waked since the task is coming.\n");
                    }
                }
                if(m_tpool->getterminate())
                {
                    return false;
                }
                task = m_tpool->top_task();
                m_tpool->pop_task();
                //printf("pop a task success.\n");
            }
            if(task)
            {
                //printf("task run in thread:%lu.\n",(unsigned long)pthread_self());
                task();
            }
            return true;
        }
        static void* threadproc(void* param)
        {
            //printf("starting the thread proc.\n");
            boost::shared_ptr<worker_thread> worker(new worker_thread((threadpoolimp*)param));
            if(worker)
            {
                worker->run();
            }
            return 0;
        }
        
        static bool create_and_attach(threadpoolimp* pool)
        {
            pthread_t pid;
            core::common::locker_guard lg(pool->m_locker);
            if(0==pthread_create(&pid,NULL,threadproc,pool))
            {
                //printf("new thread pid is %lu.\n",(unsigned long)pid);
                pool->push_thread(pid);
                return true;
            }
            return false;
        }
    };

}

#endif
