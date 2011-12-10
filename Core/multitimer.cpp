#include "multitimer.h"
#include <boost/function.hpp>
namespace threadpool
{
int multitimer::timer_counter = 0;
multitimer::multitimer(const task_type& task,int delaysec,bool repeat)
    :m_timertask(task),m_delaysec(delaysec),
    m_elapse(0),m_repeat(repeat)
{
    m_inuse = (m_timertask==NULL)?false:true;
    m_timerid = ++timer_counter;
}
void multitimer::add_elapse(int elapse)
{
    if(!inuse())
        return;
    if(m_elapse>m_delaysec)
        return;
    m_elapse += elapse;
}
bool multitimer::isready()
{
    if(!inuse())
        return false;
    if(m_elapse>=m_delaysec)
    {
        if(m_timertask!=NULL)
            return true;
        m_inuse = false;
    }
    return false;
}
bool multitimer::inuse()
{
    return m_inuse;
}
task_type& multitimer::get_timertask_and_reset()
{
    m_elapse = 0;
    if(!m_repeat)
    {
        m_inuse = false;
    }
    return m_timertask;
}
int multitimer::getuniqueid()
{
    return m_timerid;
}
void multitimer::deletetimer()
{
    m_elapse = 0;
    m_timertask = NULL;
    m_inuse = false;
}
}

