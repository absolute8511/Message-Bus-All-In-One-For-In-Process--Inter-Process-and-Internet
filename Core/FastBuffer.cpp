#include "FastBuffer.h"
#include <assert.h>
#include <stdio.h>

#define DEFAULT_SIZE 128
#define SHRINK_SIZE  1024*DEFAULT_SIZE

namespace core { namespace net {

FastBuffer::FastBuffer()
    :m_readstart(0),
    m_writestart(0),
    m_halfcounter(0)
{
    m_innerdata.resize(DEFAULT_SIZE, 0);
}

FastBuffer::~FastBuffer()
{
    clear();
}

void FastBuffer::push_back(const char* pdata, size_t datasize)
{
    if(datasize == 0)
        return;
    if(m_innerdata.size() > SHRINK_SIZE)
    {
        // auto shrink if there is much free space for a long time.
        if(m_innerdata.size() - size() - datasize > SHRINK_SIZE/8*7)
        {
            if(m_halfcounter++ > 10)
            {
                printf("shrinking fastbuffer :%zu, used:%zu\n", m_innerdata.size(), size());
                m_halfcounter = 0;
                if(m_readstart > 0)
                {
                    std::copy(m_innerdata.begin() + m_readstart, m_innerdata.begin() + size(), m_innerdata.begin());
                }
                m_writestart = size();
                m_readstart = 0;
                m_innerdata.resize(m_innerdata.size()/2);
            }
        }
        else
        {
            m_halfcounter = 0;
        }
    }
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

} }
