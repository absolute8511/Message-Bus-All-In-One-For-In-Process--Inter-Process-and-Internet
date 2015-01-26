#ifndef TIMER_THREAD_H_MYIDENTIFY_1985
#define TIMER_THREAD_H_MYIDENTIFY_1985
#include "scope_guard.hpp"
#include "threadpoolimp.h"
#include "multitimer.h"
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>
namespace threadpool
{
    typedef boost::function<void()> task_type;
    class timer_thread
    {
    private:
        threadpoolimp* m_tpool;
        sigset_t m_mask;
    public:
        timer_thread(threadpoolimp* pool)
            :m_tpool(pool)
        {
        }
        //等待msec毫秒数，返回等待未满足的毫秒数（等待可能被中断）
        static unsigned wait(unsigned msec)
        {
            int n;
            struct timeval tv;
            tv.tv_sec = msec/1000;
            tv.tv_usec = (msec - msec/1000*1000)*1000;
            struct timeval start,end;
            gettimeofday(&start,NULL);
            n = select(0,NULL,NULL,NULL,&tv);
            if(n==0)
                return 0;
            //select return -1 indicate a signal is catched.
            gettimeofday(&end,NULL);
            struct timeval waited;
            timersub(&end, &start,&waited);
            unsigned waitmsec = waited.tv_sec*1000 + waited.tv_usec/1000;
            //printf("wait wrong,waited only %d ms.\n",waitmsec);
            if(waitmsec<msec)
                return msec-waitmsec;
            return 0;
        }
        void run()
        {
            //使用信号方式实现定时器
           /* sigemptyset(&m_mask);
            sigaddset(&m_mask,SIGALRM);
            sigaddset(&m_mask,SIGQUIT);*/
            scope_guard notify_exception(boost::bind(&timer_thread::died_unexpect,this));
            while(timer_proc()){}
            notify_exception.disable();
            //printf("timer_thread end in normal way.\n");
        }
        void died_unexpect()
        {
            printf("thread died unexpect.\n");
            create_and_attach(m_tpool);
        }
        bool timer_proc()
        { 
            //int signo;
            if(m_tpool->getterminate())
                return false;

            int waitms = 1000;
            while(waitms > 0) {
              waitms = wait(waitms);
            }
            if(m_tpool->getterminate())
                return false;
            //printf("timer proc in thread: %lu.\n",(unsigned long)pthread_self());
            //没有必要用锁，因为定时器容量不会改变
            //locker_guard lg(m_tpool->m_locker);
            size_t size = m_tpool->m_timertask_container.size();
            for(size_t i=0;i<size;i++)
            {
                multitimer& readytimer = m_tpool->m_timertask_container[i];
                readytimer.add_elapse(1);
                if(readytimer.isready())
                {
                    //printf("timer:%d is ready for run.\n",readytimer.getuniqueid());
                    m_tpool->push_task(readytimer.get_timertask_and_reset());
                    m_tpool->notifyone_task_coming_event();
                }
            }
            /*
            //使用信号方式实现定时器
            //sigwait(&m_mask,&signo);
            //
            //printf("signal coming:%d\n",signo);
             * if(signo==SIGALRM)
            {
                printf("timer proc in thread: %lu.\n",(unsigned long)pthread_self());
                locker_guard lg(m_tpool->m_locker);
                size_t size = m_tpool->m_timertask_container.size();
                for(size_t i=0;i<size;i++)
                {
                    multitimer& readytimer = m_tpool->m_timertask_container[i];
                    readytimer.add_elapse(1);
                    if(readytimer.isready())
                    {
                        printf("timer:%d is ready for run.\n",readytimer.getuniqueid());
                        m_tpool->push_task(readytimer.get_timertask_and_reset());
                        m_tpool->notifyone_task_coming_event();
                    }
                }
            }
            else if(signo == SIGQUIT)
            {
                printf("timer proc exit!\n");
                return false;
            }*/

            return true;
        }
        static void * threadproc(void* param)
        {
            //printf("starting the timer_thread proc.\n");
            boost::shared_ptr<timer_thread> mytimer(new timer_thread((threadpoolimp*)param));
            if(mytimer)
            {
                //使用信号方式实现定时器
                /*struct itimerval timerval = {{0},{0}};
                timerval.it_interval.tv_sec = 1;
                timerval.it_value.tv_sec = 1;
                setitimer(ITIMER_REAL,&timerval,NULL);*/
                mytimer->run();
            }
            return 0;
        }
        static bool create_and_attach(threadpoolimp* pool)
        {
            assert(pool!=NULL);
            if(pool==NULL)
                return false;
            pthread_t pid;
            core::common::locker_guard lg(pool->m_locker);
            if(0==pthread_create(&pid,NULL,threadproc,pool))
            {
                pool->push_thread(pid);
                return true;
            }
            return false;
        }
    };
}

#endif
