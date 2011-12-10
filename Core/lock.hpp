#ifndef MY_LOCK_H_MYIDENTIFY_1985
#define MY_LOCK_H_MYIDENTIFY_1985
#include <pthread.h>
namespace core { namespace common
{
    class locker
    {
    private:
      //  pthread_mutexattr_t m_attr;
        pthread_mutex_t m_mutex;
    public:
        locker()
        {
        //    pthread_mutexattr_init(&m_attr);
          //  pthread_mutexattr_settype(&m_attr,PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&m_mutex,NULL/*&m_attr*/);
        }
        ~locker()
        {
            pthread_mutex_destroy(&m_mutex);
            //pthread_mutexattr_destroy(&m_attr);
        }
        void lock()
        {
            pthread_mutex_lock(&m_mutex);
        }
        void unlock()
        {
            pthread_mutex_unlock(&m_mutex);
        }
        pthread_mutex_t& getmutex()
        {
            return m_mutex;
        }
    };
    class locker_guard
    {
    private:
        locker& m_locker;
    public:
        locker_guard(const volatile locker& mylocker)
            :m_locker(*const_cast<locker*>(&mylocker))
        {
            m_locker.lock();
        }
        ~locker_guard()
        {
            m_locker.unlock();
        }
   };
}
}
#endif
