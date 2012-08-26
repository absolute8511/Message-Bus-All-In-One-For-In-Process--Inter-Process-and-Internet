#include "named_worker_thread.h"
#include "threadpoolimp.h"
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <deque>

namespace threadpool
{
void named_worker_thread::remove_self_from_threadpool()
{
    m_terminate = true;
    m_tpool->remove_named_thread(m_threadname);
}
bool named_worker_thread::execute_task()
{
    task_type task;
    {
        core::common::locker_guard guard(m_locker);
        while(m_task_container.empty())
        {
            if(m_terminate)
            {
                return false;
            }
            else
            {
                m_task_coming_event.wait(m_locker);
            }
        }
        if(m_terminate)
        {
            return false;
        }
        task = m_task_container.front();
        m_task_container.pop_front();
    }
    if(task)
    {
        task();
    }
    return true;
}
void named_worker_thread::run()
{
    scope_guard notify_exception(boost::bind(&named_worker_thread::died_unexpect,this));
    while(execute_task()) {}
    notify_exception.disable();
    //printf("named thread end in normal way.\n");
}
//if thread died unexpected,we should recreate it.
void named_worker_thread::died_unexpect()
{
    printf("named thread: %s died unexpect.\n", m_threadname.c_str());
    assert(0);
    remove_self_from_threadpool();
}
void* named_worker_thread::threadproc(void* param)
{
    if(param == NULL)
        return 0;
    boost::shared_ptr<named_worker_thread> worker = static_cast<named_worker_thread*>(param)->shared_from_this();
    if(worker)
    {
        worker->run();
    }
    worker->remove_self_from_threadpool();
    return 0;
}

named_worker_thread::named_worker_thread(const std::string& threadname, threadpoolimp* ptpool)
    :m_threadname(threadname),
    m_tpool(ptpool),
    m_terminate(false)
{
}
void named_worker_thread::terminate_thread()
{
    core::common::locker_guard guard(m_locker);
    m_terminate = true;
    m_task_coming_event.notify_all();
}
bool named_worker_thread::queue_task(const task_type& task)
{
    core::common::locker_guard guard(m_locker);
    m_task_container.push_back(task);
    m_task_coming_event.notify_one();
    return true;
}
bool named_worker_thread::create_and_attach(threadpoolimp* pool, const std::string& threadname)
{
    pthread_t pid;
    boost::shared_ptr<named_worker_thread> newworker(new named_worker_thread(threadname, pool));
    if(0==pthread_create(&pid,NULL,threadproc, newworker.get()))
    {
        //printf("new _named_ thread : %s, pid is %lu.\n", threadname.c_str(), (unsigned long)pid);
        pool->add_named_thread(pid, threadname, newworker);
        return true;
    }
    return false;
}


}

