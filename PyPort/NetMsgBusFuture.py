#! /usr/bin/env python
# -*- coding: utf8 -*- 

import threading
import time

class Future:
    def __init__(self, callback = None):
        self.ready = False
        self.rsp = ''
        self.lock = threading.Lock()
        self.cond = threading.Condition(self.lock)
        self.create_time = time.time()
        self.callback = callback
        self.has_err = False

    def is_bad(self):
        return time.time() - self.create_time > 120

    def set_err(self, errinfo):
        self.has_err = True
        self.set_result(errinfo)

    def set_result(self, rsp):
        with self.lock:
            self.rsp = rsp
            self.ready = True
            self.cond.notify_all()
        if callable(self.callback):
            self.callback(self)

    def get(self, timeout = None):
        if timeout is None:
            return self.rsp
        with self.lock:
            if not self.ready:
                self.cond.wait(timeout)
            if self.ready:
                return self.rsp
        return None


class FutureMgr:
    def __init__(self):
        self.future_map = {}
        self.future_map_lock = threading.Lock()
        self.futureid = 0

    def GetFuture(self, callback = None):
        with self.future_map_lock:
            self.futureid += 1
            if self.futureid > 100000000:
                self.futureid = 1
            futureid = self.futureid
            new_future = Future(callback)
            self.future_map[futureid] = new_future
        return (futureid, new_future)

    def RemoveFuture(self, futureid):
        with self.future_map_lock:
            if futureid in self.future_map.keys():
                del self.future_map[futureid]

    def ClearBadFuture(self):
        with self.future_map_lock:
            for k,v in self.future_map.items():
                if v.is_bad():
                    del self.future_map[k]

    def PopFuture(self, futureid):
        with self.future_map_lock:
            if futureid not in self.future_map.keys():
                return None
            return self.future_map.pop(futureid)  

    def ClearFuture(self):
        with self.future_map_lock:
            self.future_map.clear()

