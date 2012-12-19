#! /usr/bin/env python
# -*- coding: utf8 -*- 

import threading
import asyncore, socket
import time
import logging
from NetMsgBusDataDef import *
import LocalMsgBus

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

class ReceiverChannel(asyncore.dispatcher):
    def __init__(self, sock, addr, sockmap):
        asyncore.dispatcher.__init__(self, sock=sock, map=sockmap)
        self.addr = addr
        self.last_active = time.time()
        self.buffer = ''

    def handle_close(self):
        self.close()
        log.info('client: %s closed', self.addr)

    def handle_read(self):
        self.last_active = time.time()
        self.ReceivePack()

    def is_timeout(self):
        if time.time() - self.last_active > 45:
            log.debug('client no active long time, close it %s ', self.addr)
            return True
        return False

    def readable(self):
        return True

    def writable(self):
        return (len(self.buffer) > 0)

    def handle_write(self):
        log.debug('begin write data to receiver client: %s', self.addr)
        sent = self.send(self.buffer)
        self.buffer = self.buffer[sent:]
        self.last_active = time.time()

    def handle_error(self):
        self.close()
        log.error('client connection has error : %s', self.addr)

    def ReceivePack(self):
        msg_pack = ReceiverSendMsgReq()
        headdata = self.recv(msg_pack.HeadSize())
        msg_pack.UnPackHead(headdata)
        msgcontent = self.recv(msg_pack.data_len)
        msg_pack.UnPackBody(msgcontent)
        # 消息格式必须是 msgid=消息标示串＆msgparam=具体的消息内容 
        # 具体的消息内容可以是JSON/XML数据格式(或者也可以是二进制数据)，具体由收发双方协定
        if msg_pack.sync_sid > 0:
            log.debug("got a receiver client request, syncflag:%d, sid:%d, client:%s", msg_pack.is_sync, msg_pack.sync_sid, self.addr)
            self.NetMsgBusRspSendMsg(msg_pack)

    def NetMsgBusRspSendMsg(self, msg_pack):
        msgid = ReceiverMsgUtil.GetMsgId(msg_pack.data)
        if len(msgid) > 0:
            msgparam = ReceiverMsgUtil.GetMsgParam(msg_pack.data)
            msgparam = LocalMsgBus.SendMsg(msgid, msgparam)
            if msgparam is None:
                log.debug('handle receiver msgid %s request failed. ', msgid)
                msgparam = ''
            rsp_pack = ReceiverSendMsgRsp()
            rsp_pack.sync_sid = msg_pack.sync_sid
            rsp_pack.SetRspData(msgparam)
            self.buffer += rsp_pack.PackData()
            log.debug("process a sync request finished, sid:%d, msgid:%s, msgparam:%s", msg_pack.sync_sid, msgid, msgparam);

class NetMsgBusReceiverMgr(asyncore.dispatcher):

    def __init__(self, receiver_ip, receiver_port, sockmap):
        asyncore.dispatcher.__init__(self, map=sockmap)

        self.receiver_ip = receiver_ip
        self.receiver_port = receiver_port
        self.buffer = ''
        self.need_stop = False
        self.is_closed = False
        self.sockmap = sockmap

    def StartReceiver(self):
        self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
        self.set_reuse_addr()
        self.bind(('', self.receiver_port))
        self.listen(16)
        log.info('receiver begin listen on port: %d', self.receiver_port)

    def StopReceiver(self):
        self.need_stop = True
        log.debug('stopping receiver server ')

    def doloop(self):
        try:
            asyncore.loop(timeout=5, count=1, map=self.sockmap)
        except:
            log.info('receiver server loop exception')

        for c in self.sockmap.values():
            if c.is_timeout():
                c.close()

        if self.need_stop:
            self.close()
            self.is_closed = True
            log.debug('close all clients from loop')
            for c in self.sockmap.values():
                c.close()

    def readable(self):
        return True

    def is_timeout(self):
        return False

    def handle_accept(self):
        pair = self.accept()
        if pair is None:
            pass
        else:
            conn, addr = pair
            log.info('new client connected to receiver: %s', addr)
            ReceiverChannel(conn, addr, self.sockmap)

    def handle_close(self):
        self.close()
        self.is_closed = True
        log.info('receiver server %s closed', self.server_ip)
        return True

    def handle_error(self):
        self.close()
        self.is_closed = True
        log.error('receiver server connection has error')
        
class ReceiverMgrServerRunner(threading.Thread):
    def __init__(self, receivermgr):
        threading.Thread.__init__(self)
        self.receivermgr = receivermgr

    def run(self):
        self.receivermgr.StartReceiver()
        while not self.receivermgr.is_closed:
            self.receivermgr.doloop()

