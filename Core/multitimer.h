#ifndef MULTITIMER_H_MYIDENTIFY_1985
#define MULTITIMER_H_MYIDENTIFY_1985
#include <boost/function.hpp>
namespace threadpool
{
    typedef boost::function<void()> task_type;
    //used for multi timers.
    class multitimer
    {
    private:
        task_type m_timertask;
        int m_delaysec;
        int m_elapse;
        bool m_inuse;
        bool m_repeat;
        int m_timerid;                   //the unique id for each timer.
        static int timer_counter;
    public:
        multitimer(const task_type& task=NULL,int delaysec=0,bool repeat=false);
        void add_elapse(int elapse);
        bool isready();
        bool inuse();
        task_type& get_timertask_and_reset();
        int getuniqueid();
        void deletetimer();
    };
}

#endif
