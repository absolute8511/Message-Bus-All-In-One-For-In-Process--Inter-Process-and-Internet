#ifndef CORE_NET_FASTBUFFER_H
#define CORE_NET_FASTBUFFER_H

#include <vector>
#include <assert.h>

namespace core { namespace net {
class FastBuffer
{
public:
    FastBuffer();
    ~FastBuffer();
    inline const char* data()
    {
        assert(m_readstart <= (int)m_innerdata.size());
        return &*m_innerdata.begin() + m_readstart;
    }
    // the valid offset of char* returned by data()
    inline size_t size() const
    {
        assert(m_writestart >= m_readstart);
        return m_writestart - m_readstart;
    }
    void  push_back(const char * pdata, size_t datasize);
    inline bool  empty() const
    {
        return m_readstart == m_writestart;
    }
    inline void  pop_front(size_t datasize)
    {
        assert(datasize <= size());
        m_readstart += datasize;
        if(m_writestart <= m_readstart)
        {
            // no valid data in the vector, we can move the index to the begin to free space
            m_readstart = m_writestart = 0;
        }
    }
    inline void  clear()
    {
        m_writestart = m_readstart = 0;
    }

private:
    int  m_readstart;
    int  m_writestart;
    std::vector<char>  m_innerdata;
    int  m_halfcounter;
};

} }
#endif
