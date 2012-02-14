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
    // get the inner data pointer for write data to it directly to enhance the performance.
    // be sure to call the push_back_withoutdata with the size that you added directly while you finish using it.
    inline char* writablebegin()
    {
        assert(m_writestart <= (int)m_innerdata.size());
        return &*m_innerdata.begin() + m_writestart;
    }
    inline const char* data()
    {
        assert(m_readstart <= (int)m_innerdata.size());
        return &*m_innerdata.begin() + m_readstart;
    }
    // make sure there is enough free space to write datasize into inner buffer.
    void ensurewritable(size_t datasize);
    void push_back_withoutdata(size_t datasize)
    {
        assert(datasize + m_writestart <= m_innerdata.size());
        m_writestart += datasize;
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
