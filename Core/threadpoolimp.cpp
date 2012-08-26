#include "threadpoolimp.h"
#include "named_worker_thread.h"

namespace threadpool
{
// the caller must make sure thread safe
void threadpoolimp::push_thread(pthread_t pid)
{
    if(m_terminateall)
        return;
    if(pid)
        m_pids.push_back(pid);
}

// the caller must make sure thread safe
bool threadpoolimp::threadempty()
{
    return m_pids.empty();
}

// the caller must make sure thread safe
void threadpoolimp::push_alonethread(pthread_t pid)
{
    if(m_terminateall)
        return;
    if(pid && (std::find_first_of(m_alone_pids.begin(), m_alone_pids.end(), &pid, &pid + 1, pthread_equal) == m_alone_pids.end()))
        m_alone_pids.push_back(pid);
}
// the caller must make sure thread safe
void threadpoolimp::erase_alonethread(pthread_t pid)
{
    if(m_terminateall)
        return;
    std::vector<pthread_t>::iterator it = m_alone_pids.begin();
    while( it != m_alone_pids.end() )
    {
        if( pthread_equal(pid, *it) != 0 )
        {
            m_alone_pids.erase(it);
            break;
        }
        ++it;
    }
}
void threadpoolimp::waittoterminate()
{
    terminateall();
    for(size_t i=0;i<m_pids.size();i++)
    {
        pthread_join(m_pids[i],NULL);
    }
    NamedThreadContainerT temp_named_threads;
    {
        core::common::locker_guard guard(m_locker);
        temp_named_threads = m_named_threads;
    }
    NamedThreadContainerT::iterator it = temp_named_threads.begin();
    while(it != temp_named_threads.end())
    {
        if(it->second.first)
        {
            it->second.first->terminate_thread();
        }
        pthread_join(it->second.second, NULL);
        ++it;
    }
    for(size_t i=0;i<m_alone_pids.size();i++)
    {
        pthread_kill(m_alone_pids[i], SIGINT);
        printf("thread killed : %lu.\n", (unsigned long)m_alone_pids[i]);
    }
    //printf("wait all thread joined success.\n");
}

// caller must make sure thread safe.
void threadpoolimp::add_named_thread(pthread_t pid, const std::string& threadname, boost::shared_ptr<named_worker_thread> spworker)
{
    m_named_threads[threadname] = std::make_pair(spworker, pid);
}

bool threadpoolimp::push_task_to_named_thread(const task_type& task, const std::string& threadname)
{
    core::common::locker_guard guard(m_locker);
    if(m_terminateall)
        return false;
    NamedThreadContainerT::iterator it = m_named_threads.find(threadname);
    if(it == m_named_threads.end() || !it->second.first)
    {
        if(!named_worker_thread::create_and_attach(this, threadname))
            return false;
    }
    it = m_named_threads.find(threadname);
    if(it != m_named_threads.end() && it->second.first)
    {
        it->second.first->queue_task(task);
        return true;
    }
    return false;
}

void threadpoolimp::terminate_named_thread(const std::string& threadname)
{
    core::common::locker_guard guard(m_locker);
    NamedThreadContainerT::iterator it = m_named_threads.find(threadname);
    if(it != m_named_threads.end())
    {
        if(it->second.first)
        {
            it->second.first->terminate_thread();
        }
    }
}

bool threadpoolimp::get_named_thread(const std::string& threadname, pthread_t& pid)
{
    core::common::locker_guard guard(m_locker);
    NamedThreadContainerT::iterator it = m_named_threads.find(threadname);
    if(it == m_named_threads.end() || !it->second.first)
    {
        return false;
    }
    pid = it->second.second;
    return true;
}

// 仅仅删除线程对象的引用, called when the named thread quit normally.
void threadpoolimp::remove_named_thread(const std::string& threadname)
{
    core::common::locker_guard guard(m_locker);
    NamedThreadContainerT::iterator it = m_named_threads.find(threadname);
    if(it != m_named_threads.end())
    {
        m_named_threads.erase(it);
    }
}

}

