#include "threadpool.h"
#include "lock.hpp"
#include "multitimer.h"
#include "threadpoolimp.h"
#include "worker_thread.hpp"
#include "timer_thread.hpp"
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>

#ifndef THREAD_POOL_MAX_THREAD
#define THREAD_POOL_MAX_THREAD 64
#endif

namespace threadpool
{
    static threadpoolimp tpool;
    static volatile bool s_threadpool_running = false;
    void* longrunproc(void* param)
    {
        task_type longtask = *((task_type*)param);
        if(longtask)
            longtask();

        core::common::locker_guard guard(tpool.m_locker);
        tpool.erase_alonethread(pthread_self());
        return 0;
    }

    bool queue_work_task_to_named_thread(task_type const& task, const std::string& threadname)
    {
        if(!s_threadpool_running)
            return false;
        return tpool.push_task_to_named_thread(task, threadname);
    }

    bool queue_work_task(task_type const& task,int flag)
    {
        if(!s_threadpool_running)
        {
            assert(false);
            return false;
        }
        if( flag == 1)
        {// need an alone thread to excute the task.
            pthread_t pid;
            core::common::locker_guard guard(tpool.m_locker);
            if(pthread_create(&pid, NULL, longrunproc, (void*)&task)==0)
            {
                tpool.push_alonethread(pid);
                pthread_detach(pid);
                return true;
            }
            return false;
        }
        tpool.push_task(task);
        return true;
    }
    //set a timer task,attention: not very accurate.
    int queue_timer_task(task_type const& task,int delaysec,bool repeatflag)
    {
        if(task==NULL)
            return 0;
        if(!s_threadpool_running)
            return false;
        size_t i=0;
        //printf("find a position to put the timer task.\n");
        core::common::locker_guard lg(tpool.m_locker);
        size_t size = tpool.m_timertask_container.size();
        while(i<size && tpool.m_timertask_container[i].inuse())
        {
            ++i;
        }
        if(i==size)
            return 0;
        tpool.m_timertask_container[i] = multitimer(task,delaysec,repeatflag);
        return tpool.m_timertask_container[i].getuniqueid();
    }
    void deletetimerbyid(int timerid)
    {
        size_t i=0;
        core::common::locker_guard lg(tpool.m_locker);
        size_t size = tpool.m_timertask_container.size();
        while(i<size)
        {
            if(tpool.m_timertask_container[i].getuniqueid() == timerid)
            {
                tpool.m_timertask_container[i].deletetimer();
                return;
            }
            ++i;
        }
    }
    bool init_thread_pool(int thread_num)
    {
        /*
        //使用信号方式实现定时器
         * sigemptyset(&mask);
        sigaddset(&mask,SIGALRM);
        //主线程屏蔽SIGALRM信号，使用单独的线程来处理该信号用于定时任务，防止主线程被信号中断
        if(pthread_sigmask(SIG_BLOCK,&mask,&oldmask)!=0)
            return false;
        */
        if(s_threadpool_running)
            return true;
        timer_thread::create_and_attach(&tpool);
        if(thread_num <= 0)
            thread_num = THREAD_POOL_MAX_THREAD;

        for(int i = 0; i < thread_num; i++)
        {
            worker_thread::create_and_attach(&tpool);
        }
        //no thread can be created,failed to initialize thread pool.
        if(tpool.threadempty())
            return false;
        s_threadpool_running = true;
        return true;
    }
    void destroy_thread_pool()
    { 
        tpool.terminateall();
        tpool.waittoterminate();
        s_threadpool_running = false;
    }
}
