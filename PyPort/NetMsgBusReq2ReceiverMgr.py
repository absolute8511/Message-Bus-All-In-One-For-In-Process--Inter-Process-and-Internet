#! /usr/bin/env python
# -*- coding: utf8 -*- 

import threading
import asyncore, socket
import time
from time import sleep
import logging
from NetMsgBusDataDef import *
from LocalMsgBus import *
from NetMsgBusFuture import *

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

class Req2ReceiverChannel(asyncore.dispatcher):
    def __init__(self, destip, destport, sockmap, req2receivermgr):
        asyncore.dispatcher.__init__(self, map=sockmap)
        self.dest_ip = destip
        self.dest_port = destport
        self.writelock = threading.Lock()
        self.buffer = ''
        self.read_buffer = ''
        self.need_stop = False
        self.is_closed = False
        self.req2receivermgr = req2receivermgr
        self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
        log.debug('connecting to receiver %s:%d', destip, destport)
        self.connect( (destip, destport) )

    def handle_connect(self):
        pass

    def handle_close(self):
        self.req2receivermgr.handle_channel_close()
        self.close()
        self.is_closed = True
        log.info('receiver connection to %s:%d closed', self.dest_ip, self.dest_port)

    def handle_read(self):
        if self.need_stop:
            self.close()
            self.is_closed = True
            return
        self.ReceivePack()

    def readable(self):
        return True

    def writable(self):
        return (len(self.buffer) > 0)

    def handle_write(self):
        if self.need_stop:
            self.close()
            self.is_closed = True
            return
        if not self.connected:
            return
        with self.writelock:
            sent = self.send(self.buffer)
            self.buffer = self.buffer[sent:]

    def handle_error(self):
        self.req2receivermgr.handle_channel_close()
        self.close()
        self.is_closed = True
        log.error('!!!! receiver channel connection has error !!!!!')

    def ReceivePack(self):
        try:
            tmpbuf = self.recv(4098)
        except socket.error, why:
            log.debug('==== receiver data exception: %s', why)
            if len(self.read_buffer) == 0:
                return
        self.read_buffer += tmpbuf
        tmpbuf = ''

        while True:
            rsp = ReceiverSendMsgRsp()
            if len(self.read_buffer) < rsp.HeadSize():
                return
            headbuffer = self.read_buffer[:rsp.HeadSize()]
            rsp.UnPackHead(headbuffer)
            if len(self.read_buffer) < rsp.HeadSize() + rsp.data_len:
                return
            rsp.data = self.read_buffer[rsp.HeadSize():rsp.data_len + rsp.HeadSize()]
            self.read_buffer = self.read_buffer[rsp.data_len + rsp.HeadSize():]
            log.debug('reading receiver rsp : future_id :%d, data:%s ', rsp.sync_sid, rsp.data)
            self.req2receivermgr.handle_channel_rsp(rsp.sync_sid, rsp.data)

class TcpClientPool:
    def __init__(self, sockmap, req2receivermgr):
        self.channels = {}
        self.sockmap = sockmap
        self.channel_lock = threading.Lock()
        self.select_cnt = 0
        self.req2receivermgr = req2receivermgr

    def CreateTcpConn(self, ipport, num=5, callback = None):
        with self.channel_lock:
            if ipport not in self.channels.keys():
                self.channels[ipport] = []
            host_channels = self.channels[ipport]
            host_channels[:] = [x for x in host_channels if not x.is_closed]
            self.select_cnt += 1
            if len(host_channels) >= num:
                return host_channels[(self.select_cnt) % len(host_channels)]
            for i in range(num - len(host_channels)):
                channel = Req2ReceiverChannel(ipport[0], ipport[1], self.sockmap, self.req2receivermgr)
                if callback is not None:
                    callback(channel)
                host_channels.append(channel)
            return host_channels[self.select_cnt % len(host_channels) ]

    def ClearAll(self):
        for ipport,host_channels in self.channels.items():
            for channel in host_channels:
                channel.need_stop = True
        self.channels.clear()

# req2task : (syncflag, retry, futurepair, timeout, (destip, desthost)/destname, data)
class NetMsgBusReq2ReceiverMgr(MsgBusHandlerBase):
    def __init__(self, server_conn_mgr):
        MsgBusHandlerBase.__init__(self)
        self.server_conn_mgr = server_conn_mgr
        self.wait2send_task = {}
        self.wait2send_task_lock = threading.Lock()
        self.task_queue = []
        self.task_queue_lock = threading.Lock()
        self.task_queue_cond = threading.Condition(self.task_queue_lock)
        self.cached_client_info = {}
        self.cache_lock = threading.Lock()
        self.future_mgr = FutureMgr()
        self.sockmap = {}
        self.tcp_conn_pool = TcpClientPool(self.sockmap, self)
        self.stop = False
        #self.AddHandler("netmsg.sever.rsp.getclient", self.HandleRspGetClient)

    def Stop(self):
        with self.task_queue_lock:
            self.stop = True
            self.task_queue_cond.notify()

    def SendMsgDirectToClient(self, ipport_or_name, data, timeout):
        if self.stop:
            return (False, None)
        if isinstance(ipport_or_name, str):
            if self.GetCachedClient(ipport_or_name) is None:
                destclient = self.server_conn_mgr.ReqReceiverInfo(ipport_or_name)
                if destclient is None:
                    return False
                with self.cache_lock:
                    self.cached_client_info[ipport_or_name] = destclient;
                ipport_or_name = destclient

        task = {'sync':True, 'retry':False, 'future':self.future_mgr.GetFuture(), 'timeout':timeout, 'dest':ipport_or_name, 'data':data}
        return self.ProcessReqToReceiver(task)

    def PostMsgDirectToClient(self, ipport_or_name, data, callback = None):
        if self.stop:
            return None
        future_pair = self.future_mgr.GetFuture(callback)
        retry = True
        if isinstance(ipport_or_name, tuple):
            # using (ip, port) no retry getting host info need.
            retry = False
        task = {'sync':False, 'retry':retry, 'future':future_pair, 'timeout':None, 'dest':ipport_or_name, 'data':data}
        self.QueueReqTaskToReceiver(task)
        #log.debug('post task and return futureid:%d ', future_pair[0])
        return future_pair[1]

    def ClearData(self):
        self.wait2send_task.clear()
        self.task_queue[:] = []
        self.future_mgr.ClearFuture()
        self.tcp_conn_pool.ClearAll()

    def handle_channel_close(self):
        log.debug('handle a receiver channel closed')
        self.future_mgr.ClearBadFuture();

    def handle_channel_rsp(self, futureid, data):
        future_rsp = self.future_mgr.PopFuture(futureid)
        if future_rsp is not None:
            future_rsp.set_result(data)
        else:
            log.warn('future %d not exist ', futureid)

    def HandleRspGetClient(self, futuredata):
        (ret_code, clientname, hostinfo) = futuredata.get() 
        log.debug('handle rsp of get client:%s', clientname)
        pendingtasks = []
        with self.wait2send_task_lock:
            if clientname in self.wait2send_task.keys():
                pendingtasks = self.wait2send_task.pop(clientname)
            else:
                log.debug('pending task client rsp:%s, but no pending task in client', clientname)
        if ret_code == 0:
            log.debug('get client info returned. ret name : %s, ip:port : %s:%d', clientname, hostinfo[0], 
              hostinfo[1]);
            with self.cache_lock:
                self.cached_client_info[clientname] = hostinfo;
            for task in pendingtasks:
                task['retry'] = False
                self.QueueReqTaskToReceiver(task);
        else:
            LocalMsgBus.SendMsg("netmsgbus.server.getclient.error", clientname)
            log.debug('server return error while query client info, ret_code: %d.', ret_code)
            for task in pendingtasks:
                task['retry'] = False
                self.future_mgr.RemoveFuture(task['future'][0]);

    def QueueReqTaskToReceiver(self, task):
        with self.task_queue_lock:
            self.task_queue.append(task)
            self.task_queue_cond.notify()

    def QueueWaitingTask(self, task):
        with self.wait2send_task_lock:
            if task['dest'] not in self.wait2send_task.keys():
                self.wait2send_task[task['dest']] = []
            self.wait2send_task[task['dest']].append(task)
            self.server_conn_mgr.ReqReceiverInfo(task['dest'], self.HandleRspGetClient)

    def RemoveCachedClient(self, clientname):
        with self.cache_lock:
            self.cached_client_info.pop(clientname)

    def GetCachedClient(self, clientname):
        with self.cache_lock:
            if clientname not in self.cached_client_info.keys():
                return None
            return self.cached_client_info[clientname]

    def ProcessReqToReceiver(self, task):
        destclient = ('', 0)
        if (isinstance(task['dest'], tuple)):
            destclient = task['dest']
        else:
            destclient = self.GetCachedClient(task['dest'])
            log.debug('use cached client info : %s,', task['dest'])
            print destclient
            if destclient is None:
                if task['retry']:
                    self.QueueWaitingTask(task)
                else:
                    self.future_mgr.RemoveFuture(task['future'][0])
                    return (False, None)
                return (True, None)

        newtcp = self.tcp_conn_pool.CreateTcpConn(destclient, callback = self.IdentiySelfToReceiver)
        if newtcp is None:
            log.debug('tcp create failed')
            if task['retry']:
                self.QueueWaitingTask(task)
                self.RemoveCachedClient(task['dest'])
            else:
                self.future_mgr.RemoveFuture(task['future'][0])
                return (False, None)
            return (True, None)

        rsp_content = self.WriteTaskDataToReceiver(newtcp, task)
        return (True, rsp_content)
        
    def IdentiySelfToReceiver(self, newtcp):
        sendername = self.server_conn_mgr.receiver_name
        identify_data = ReceiverMsgUtil.MakeMsgNetData('', '', sendername)
        identifytask = {'sync':False, 'retry':False, 'future':(0, None), 'dest':sendername,
                'timeout':None, 'data': identify_data}
        return self.WriteTaskDataToReceiver(newtcp, identifytask)

    def WriteTaskDataToReceiver(self, newtcp, task):
        req = ReceiverSendMsgReq()
        req.is_sync = 0
        req.sync_sid = task['future'][0]
        cur_sendmsg_rsp = None
        if (task['sync']):
            req.is_sync = 1
            cur_sendmsg_rsp = task['future'][1]

        req.SetMsgData(task['data'])
        newtcp.buffer += req.PackData() 
        if newtcp.connected:
            newtcp.handle_write()
        # 如果要求同步发送， 则等待
        result = None
        if (task['sync']):
            result = cur_sendmsg_rsp.get(task['timeout'])
            if cur_sendmsg_rsp.has_err:
                log.info('sync future return err: %s', result)
                result = None
        return result

    def doloop(self):
        rtask = None
        with self.task_queue_lock:
            while len(self.task_queue) == 0:
                if self.stop:
                    return
                self.task_queue_cond.wait()
            rtask = self.task_queue.pop(0);
        self.ProcessReqToReceiver(rtask)

class Req2ReceiverChannelRunner(threading.Thread):
    def __init__(self, req2receivermgr):
        threading.Thread.__init__(self)
        self.req2receivermgr = req2receivermgr
    def run(self):
        while not self.req2receivermgr.stop:
            asyncore.loop(timeout=5, count=1, map=self.req2receivermgr.sockmap)

class Req2ReceiverMgrRunner(threading.Thread):
    def __init__(self, req2receivermgr):
        threading.Thread.__init__(self)
        self.req2receivermgr = req2receivermgr
        self.channel_runner = Req2ReceiverChannelRunner(self.req2receivermgr)
        self.channel_runner.daemon = True

    def stop(self):
        self.req2receivermgr.Stop()

    def run(self):
        self.channel_runner.start()
        while not self.req2receivermgr.stop:
            self.req2receivermgr.doloop()

        self.req2receivermgr.ClearData()
        self.channel_runner.join()

