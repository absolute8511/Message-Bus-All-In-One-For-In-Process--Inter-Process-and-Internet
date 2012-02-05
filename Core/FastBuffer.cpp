#include "FastBuffer.h"
#include <assert.h>

#define DEFAULT_SIZE 128
namespace core { namespace net {

FastBuffer::FastBuffer()
    :m_readstart(0),
    m_writestart(0)
{
    m_innerdata.resize(DEFAULT_SIZE, 0);
}

FastBuffer::~FastBuffer()
{
    clear();
}

const char* FastBuffer::data()
{
    assert(m_readstart <= m_innerdata.size());
    return &*m_innerdata.begin() + m_readstart;
}

// the valid offset of char* returned by data()
size_t FastBuffer::size() const
{
    assert(m_writestart <= m_readstart);
    return m_writestart - m_readstart;
}

bool FastBuffer::empty() const
{
    return m_readstart == m_writestart;
}

void FastBuffer::push_back(const char* pdata, size_t datasize)
{
    if(datasize == 0)
        return;
    assert(m_writestart <= m_innerdata.size());
    if(m_innerdata.size() - m_writestart < datasize)
    {
        // get more size
        m_innerdata.resize(m_writestart + datasize, 0);
        printf("resizing fastbuffer :%zu, used:%zu\n", m_innerdata.size(), size());
    }
    std::copy(pdata, pdata + datasize, m_innerdata.begin() + m_writestart);
    m_writestart += datasize;
}

void FastBuffer::pop_front(size_t datasize)
{
    assert(datasize <= size());
    m_readstart += datasize;
    if(m_writestart <= m_readstart)
    {
        // no valid data in the vector, we can move the index to the begin to free space
        m_readstart = m_writestart = 0;
    }
}

void FastBuffer::clear()
{
    m_writestart = m_readstart = 0;
}

} }
