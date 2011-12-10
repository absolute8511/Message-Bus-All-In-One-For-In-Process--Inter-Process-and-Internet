#ifndef CONDITION_H_MYIDENTIFY_1985
#define CONDITION_H_MYIDENTIFY_1985

#include <pthread.h>
#include "lock.hpp"
namespace core { namespace common 
{
    class condition
    {
    private:
        pthread_cond_t cond;
    public:
        condition()
        {
            pthread_cond_init(&cond,NULL);
        }
        ~condition()
        {
            pthread_cond_destroy(&cond);
        }
        int notify_one()
        {
            return pthread_cond_signal(&cond);
        }
        int notify_all()
        {
            return pthread_cond_broadcast(&cond);
        }
        int wait(locker& lk)
        {
            return pthread_cond_wait(&cond,&(lk.getmutex()));
        }
        int waittime(locker& lk,const struct timespec *abstime)
        {
            return pthread_cond_timedwait(&cond,&(lk.getmutex()),abstime);
        }
    };
} // namespace common
} // namespace core
#endif
