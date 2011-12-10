#ifndef CORE_COMMON_UTILITY_H
#define CORE_COMMON_UTILITY_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string>
#include <string.h>
#include <boost/shared_array.hpp>
#include <boost/noncopyable.hpp>
#include <iconv.h>
#include <errno.h>

#if defined (__APPLE__) || defined (__MACH__) 
#include <mach/mach_time.h>
#include <mach/mach.h>
#endif
#if defined (__APPLE__) || defined (__MACH__) 
#include <mach-o/dyld.h>
#endif

#if ! defined(MARKUP_SIZEOFWCHAR)
#if __SIZEOF_WCHAR_T__ == 4 || __WCHAR_MAX__ > 0x10000
#define MARKUP_SIZEOFWCHAR 4
#else
#define MARKUP_SIZEOFWCHAR 2
#endif
#endif

namespace core { 
class utility
{
public:
    static bool set_fd_nonblock(int fd)
    {
        long fdflag;
        if( (fdflag = fcntl(fd, F_GETFL, NULL)) < 0 )
        {
            perror("fcntl failed.\n");
            return false;
        }
        return fcntl(fd, F_SETFL, fdflag|O_NONBLOCK) >= 0;
    }
    static bool set_fd_close_onexec(int fd)
    {
        long fdflag;
        if( (fdflag = fcntl(fd, F_GETFD, NULL)) < 0 )
        {
            perror("fcntl failed.\n");
            return false;
        }
        bool ret = fcntl(fd, F_SETFD, fdflag|FD_CLOEXEC) >= 0;
        return ret;
    }

    // 获取单调递增的相对某个时间点的值,精度ms
    static time_t GetTickCount()
    {
#if defined (__APPLE__) || defined (__MACH__)
        static mach_timebase_info_data_t sTimebaseInfo;
        if(sTimebaseInfo.denom == 0)
        {
            mach_timebase_info(&sTimebaseInfo);
        }
        if(sTimebaseInfo.denom == 0)
        {
            return 0;
        }
        return mach_absolute_time() / 1000000 / sTimebaseInfo.denom * sTimebaseInfo.numer;
#else
        struct timespec ts;
        if( -1 == clock_gettime(CLOCK_MONOTONIC, &ts) )
        {
            perror("clock_gettime error.");
            //try again.
            clock_gettime(CLOCK_MONOTONIC, &ts);
        }
        return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
    }
    // 获取执行文件所在路径
    static std::string GetModulePath()
    {
        char buf[1024] = {0};
#if defined (__APPLE__) || defined (__MACH__) 
        uint32_t bufsize = 1024;
        int cnt = _NSGetExecutablePath(buf, &bufsize);
        cnt = bufsize;
#else
        int cnt = readlink("/proc/self/exe", buf, 1024);
#endif
        if(cnt < 0)
        {
            memset(buf, 0, 1024);
            if(NULL == getcwd(buf, 1024))
            {
                return "";
            }
        }
        else
        {
            while(buf[cnt] != '/' && cnt > 0)
                buf[cnt--] = '\0';
            char *prealpath = realpath(buf, NULL);
            if(prealpath != NULL)
            {
                std::string realpathstr(prealpath);
                free(prealpath);
                return realpathstr;
            }
        }
        return std::string(buf);
    }
};

class StrA2W: public boost::noncopyable
{
public:
    operator std::wstring() const
    {
        return std::wstring(m_buf.get());
    }
    wchar_t* data()
    {
        return m_buf.get();
    }
    ~StrA2W()
    {
    }
    StrA2W(const std::string& src, const char* charset = "GBK")
    {
        Convert(src, charset);
    }


private:
    void Convert(const std::string& src, const char* charset)
    {
        size_t srcbytes = src.size();
        m_buf.reset(new wchar_t[src.size() + 1]);
        char *wrptr = (char *) m_buf.get();
        iconv_t cd;
        size_t avail = (src.size() + 1)*sizeof(wchar_t);
#if MARKUP_SIZEOFWCHAR == 4
#if defined (__APPLE__) || defined (__MACH__) 
        // mac osx do not support wchar_t!
        cd = iconv_open ("UCS-4-INTERNAL", charset);
#else
        cd = iconv_open ("WCHAR_T", charset);
#endif
#else 
#if defined (__APPLE__) || defined (__MACH__)
        cd = iconv_open ("UCS-2-INTERNAL", charset);
#else
        cd = iconv_open ("WCHAR_T", charset);
#endif
#endif
        if (cd == (iconv_t) -1)
        {
            /* Something went wrong.  */
            if (errno == EINVAL)
                printf("conversion from '%s' to wchar_t not available",
                    charset);
            else
                perror ("iconv_open");

            *wrptr = L'\0';
            throw;
        }

        const char* psrc = src.data();
        /* Do the conversion.  */
        size_t nconv = iconv (cd, (char**)&psrc, &srcbytes, &wrptr, &avail);
        if (nconv == (size_t) -1)
        {
            perror("A2W failed.");
            throw;
        }

        /* Terminate the output string.  */
        if (avail >= sizeof (wchar_t))
            *((wchar_t *) wrptr) = L'\0';

        if (iconv_close (cd) != 0)
            perror ("iconv_close");
    }


    boost::shared_array<wchar_t> m_buf;
};

class StrW2A : public boost::noncopyable
{
public:
    operator std::string () const
    {
        return std::string(m_mbsbuf.get());
    }
    char* data()
    {
        return m_mbsbuf.get();
    }
    ~StrW2A()
    {
    }
    StrW2A(const std::wstring& src, const char* charset = "GBK")
    {
        Convert(src, charset);
    }


private:
    void Convert(const std::wstring& wcssrc, const char* charset)
    {
        size_t srcbytes = wcssrc.size()*sizeof(wchar_t);
        size_t destbytes = srcbytes;
        m_mbsbuf.reset(new char[srcbytes + 1]);
        char *mbsptr = m_mbsbuf.get();
        iconv_t cd;
#if MARKUP_SIZEOFWCHAR == 4
        // mac osx do not support wchar_t!
#if defined (__APPLE__) || defined (__MACH__)
        cd = iconv_open (charset, "UCS-4-INTERNAL");
#else
        cd = iconv_open (charset, "WCHAR_T");
#endif
#else 
#if defined (__APPLE__) || defined (__MACH__)
        cd = iconv_open (charset, "UCS-2-INTERNAL");
#else
        cd = iconv_open (charset, "WCHAR_T");
#endif
#endif
        if (cd == (iconv_t) -1)
        {
            /* Something went wrong.  */
            if (errno == EINVAL)
                printf ("conversion from wchar_t to '%s' not available",
                    charset);
            else
                perror ("iconv_open");

            *mbsptr = '\0';
            throw;
        }
        const char * psrc = (const char*)wcssrc.data();
        size_t nconv = iconv (cd, (char**)&psrc, &srcbytes, &mbsptr, &destbytes);
        if (nconv == (size_t) -1)
        {
            perror("W2A failed.");
            throw;
        }
        assert(srcbytes == 0);
        /* Terminate the output string.  */
        *((char *) mbsptr) = '\0';

        if (iconv_close (cd) != 0)
            perror ("iconv_close");
    }
    boost::shared_array<char> m_mbsbuf;

};

}

#endif
