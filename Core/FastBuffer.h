#ifndef CORE_NET_FASTBUFFER_H
#define CORE_NET_FASTBUFFER_H

#include <vector>

namespace core { namespace net {
class FastBuffer
{
public:
    FastBuffer();
    ~FastBuffer();
    const char* data();
    size_t size() const;
    void  push_back(const char * pdata, size_t size);
    bool  empty() const;
    void  pop_front(size_t size);
    void  clear();
private:
    int  m_readstart;
    int  m_writestart;
    std::vector<char>  m_innerdata;
    int  m_halfcounter;
};

} }
#endif
