#ifndef  CORE_NET_SOCKEVENT_H
#define  CORE_NET_SOCKEVENT_H

namespace core{ namespace net {

enum EventResult
{
    EV_NOEVENT   =  0,
    EV_READ      =  1,
    EV_WRITE     =  2,
    EV_EXCEPTION =  4,
    EV_ALL       =  7,
};
class SockEvent
{
public:
    SockEvent()
        :event(EV_NOEVENT)
    {
    }
    SockEvent(EventResult ev)
        :event(ev)
    {
    }
    bool hasAny() const
    {
        return event != EV_NOEVENT;
    }
    bool hasRead() const
    {
        return (event & EV_READ) != EV_NOEVENT;
    }
    bool hasWrite() const
    {
        return (event & EV_WRITE) != EV_NOEVENT;
    }
    bool hasException() const
    {
        return (event & EV_EXCEPTION) != EV_NOEVENT;
    }
    void AddEvent(EventResult er)
    {
        event = EventResult(event | er); 
    }
    void RemoveEvent(EventResult er)
    {
        event = EventResult(event & ~er);
    }
    void ClearEvent()
    {
        event = EV_NOEVENT;
    }
    EventResult GetEvent() const
    {
        return event;
    }
private:
    EventResult event;
};

} }

#endif 
