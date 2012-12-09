#! /usr/bin/env python
# -*- coding: utf8 -*- 

import threading
from socket import *
import asyncore, socket
import time
import logging
from NetMsgBusDataDef import *

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

class ReceiverChannel(asyncore.dispatcher):
    def __init__(self, sock, addr):
        asyncore.dispatcher.__init__(self, sock)
        self.addr = addr
        self.last_active = time.time()
        self.buffer = ''

    def handle_close(self):
        self.close()
        log.info('client: %s closed', self.addr)

    def handle_read(self):
        self.last_active = time.time()
        log.debug('begin read data from client :%s ', self.addr)
        self.ReceivePack()

    def readable(self):
        if (time.time() - self.last_active > 45):
            log.debug('client no active long time, close it %s ', self.addr)
            self.close()
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
        log.debug('receiver message head: %d, ', msg_pack.HeadSize(), )
        headdata = self.recv(msg_pack.HeadSize())
        print ','.join('%#04x' % ord(c) for c in headdata)
        msg_pack.UnPackHead(headdata)
        msgcontent = self.recv(msg_pack.data_len)
        msg_pack.UnPackBody(msgcontent)
        # 消息格式必须是 msgid=消息标示串＆msgparam=具体的消息内容 
        # 具体的消息内容可以是JSON/XML数据格式(或者也可以是二进制数据)，具体由收发双方协定
        # 第一次连接后必须先发一个包含msgsender的消息串表明自己的身份
        log.debug("got sendmsg data, syncflag:%d, sync_sid:%d, data_len:%d, sender:%s.", msg_pack.is_sync, msg_pack.sync_sid, msg_pack.data_len, msg_pack.CheckMsgSender());
        if msg_pack.is_sync > 0:
            log.debug("got a sync request, sid:%d, client:%s", msg_pack.sync_sid, self.addr)
            self.NetMsgBusRspSendMsg(msg_pack)
        else:
            log.debug('got a non sync sendmsg')
            #NetMsgBusToLocalMsgBus(msgcontent);

    def NetMsgBusRspSendMsg(self, msg_pack):
        msgid = msg_pack.CheckMsgId()
        if msgid is not None:
            msgparam = msg_pack.GetMsgParam()
            if msgparam is not None:
                # SendMsg(msgid, msgparam)
                rsp_pack = ReceiverSendMsgRsp()
                rsp_pack.sync_sid = msg_pack.sync_sid
                rsp_pack.SetRspData(msgparam)
                self.buffer += rsp_pack.PackData()
                log.debug("process a sync request finished, sid:%d", sync_sid);
                return

class NetMsgBusReceiverMgr(asyncore.dispatcher):

    def __init__(self, receiver_ip, receiver_port):
        asyncore.dispatcher.__init__(self)

        self.receiver_ip = receiver_ip
        self.receiver_port = receiver_port
        self.buffer = ''
        self.need_stop = False
        self.is_closed = False

    def StartReceiver(self):
        self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
        self.bind(('', self.receiver_port))
        self.listen(16)
        log.info('receiver begin listen on port: %d', self.receiver_port)

    def StopReceiver(self):
        self.need_stop = True
        log.debug('stopping receiver server ')

    def doloop(self):
        asyncore.loop(timeout=1)
        if self.need_stop:
            self.close()
            self.is_closed = True

    def handle_accept(self):
        pair = self.accept()
        if pair is None:
            pass
        else:
            conn, addr = pair
            log.info('new client connected to receiver: %s', addr)
            ReceiverChannel(conn, addr)

    def handle_close(self):
        self.close()
        self.is_closed = True
        log.info('receiver server %s closed', self.server_ip)

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

