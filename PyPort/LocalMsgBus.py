#! /usr/bin/env python
# -*- encoding:utf8 -*-

import weakref
import threading
import copy
import inspect
import logging

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)
# a message handler can be a function or an object with the function named OnMsg.
# both function need like as  func(msgid, msgparam) and 
# return a pair (return_param, is_continue)
class MsgBus:
    def __init__(self):
        self.all_handlers = {}
        self.lock = threading.Lock()

    @staticmethod
    def GetCallableHandler(handler):
        if inspect.ismethod(handler):
            # we can not hold a method for later use, because this
            # will hold a reference which will cause a leak.
            return None

        if isinstance(handler, weakref.ref):
            o = handler()
            if o is None:
                return None
            if hasattr(o, 'OnMsg'):
                return (o, True)
            return (o, False)

        func = None
        if callable(handler):
            func = handler

        if not hasattr(handler, 'OnMsg'):
            if func is None:
                return None
            return (func, False)

        handler_func = getattr(handler, 'OnMsg')
        if not callable(handler_func):
            if func is None:
                return None
            return (func, False)
        return (handler, True)

    def AddHandler(self, msgid, handler):
        pair = MsgBus.GetCallableHandler(handler)
        if (pair is None) or (pair[0] is None):
            log.debug('wrong handler, not a function nor an object with callable OnMsg function')
            return False

        with self.lock:
            if msgid not in self.all_handlers.keys():
                self.all_handlers[msgid] = []
            for h in self.all_handlers[msgid]:
                ret_pair = MsgBus.GetCallableHandler(h)
                if (ret_pair[0] is not None) and id(ret_pair[0]) == id(pair[0]):
                    log.debug('handler has already been registered.')
                    return True
            self.all_handlers[msgid].append(weakref.ref(pair[0]))
        log.debug('handler registere success. msgid: %s', msgid)
        return True

    def RemoveHandler(self, msgid, handler):
        if msgid not in self.all_handlers.keys():
            return
        param_pair = MsgBus.GetCallableHandler(handler)
        if param_pair is None or (param_pair[0] is None):
            return
        with self.lock:
            weak_handlers = self.all_handlers[msgid]
            for h in weak_handlers:
                pair = MsgBus.GetCallableHandler(h)
                if pair is not None and id(pair[0]) == id(param_pair[0]):
                    weak_handlers.remove(h)
                    return

    def DoMsgHandler(self, msgid, msgparam, handlers):
        original_param = copy.copy(msgparam)
        msgparam = None
        is_continue = True
        for h in handlers:
            copyed_param = copy.copy(original_param)
            if h[1]:
                #log.debug('====== object with OnMsg handler ===== ')
                (msgparam, is_continue) = h[0].OnMsg(msgid, copyed_param)
            else:
                #log.debug('====== function handler ===== ')
                (msgparam, is_continue) = h[0](msgid, copyed_param)
            if not is_continue:
                return msgparam
        return msgparam

    def SendMsg(self, msgid, msgparam):
        strong_handlers = []
        #log.debug('begin handle msgid: %s', msgid)
        with self.lock:
            if msgid not in self.all_handlers.keys():
                log.debug('msgid: %s no handlers found.', msgid)
                return None
            weak_handlers = self.all_handlers[msgid]
            for weak_h in weak_handlers:
                o = MsgBus.GetCallableHandler(weak_h)
                if o is not None and o[0] is not None:
                    strong_handlers.append(o)
            # using slice assign to remove in place.
            weak_handlers[:] = [h for h in weak_handlers if h() is not None]

        return self.DoMsgHandler(msgid, msgparam, strong_handlers)

g_msgbus = MsgBus()

def SendMsg(msgid, msgparam):
    return g_msgbus.SendMsg(msgid, msgparam)

def AddHandler(msgid, handler):
    return g_msgbus.AddHandler(msgid, handler)

def RemoveHandler(msgid, handler):
    return g_msgbus.RemoveHandler(msgid, handler)

class MsgBusHandlerBase:
    def __init__(self):
        self.all_dispatchers = {}

    def AddHandler(self, msgid, dispatcher):
        if not callable(dispatcher):
            log.debug('dispatcher in handler object is not callable')
            return False
        self.all_dispatchers[msgid] = dispatcher
        return AddHandler(msgid, self)

    def OnMsg(self, msgid, msgparam):
        if msgid not in self.all_dispatchers.keys():
            log.debug('msgid :%s not found in dispatchers', msgid)
            return
        return self.all_dispatchers[msgid](msgid, msgparam)
