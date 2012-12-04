#! /usr/bin/env python
# -*- coding: utf8 -*- 

import string
import threading
from socket import *
import asyncore, socket
import time
from time import sleep
import logging
from NetMsgBusDataDef import *

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

class NetMsgBusServerConnMgr(asyncore.dispatcher):

    def __init__(self, server_ip, server_port, receiver_ip, receiver_port, receiver_name):
        asyncore.dispatcher.__init__(self)
        self.rsp_handlers = {
                kMsgBusBodyType.RSP_CONFIRM_ALIVE : lambda x : self.HandleRspConfirmAlive(x),
                kMsgBusBodyType.RSP_REGISTER : lambda x : self.HandleRspRegister(x),
                kMsgBusBodyType.RSP_UNREGISTER : lambda x : self.HandleRspUnRegister(x),
                #kMsgBusBodyType.RSP_GETCLIENT : HandleRspGetClient,
                #kMsgBusBodyType.RSP_SENDMSG : HandleRspSendMsg,
                #kMsgBusBodyType.REQ_SENDMSG : HandleReqSendMsg,
                #kMsgBusBodyType.BODY_PBTYPE : HandleRspPBBody,
                kMsgBusBodyType.UNKNOWN_BODY : lambda x : self.HandleUnknown(x)
        }

        self.server_ip = server_ip
        self.server_port = server_port
        self.buffer = ''
        self.last_active = time.time()
        self.need_stop = False
        self.is_closed = False
        self.isreceiver_registered = False
        self.receiver_name = ''
        self.RegisterNetMsgBusReceiver(receiver_ip, receiver_port, receiver_name, kServerBusyState.LOW)
        self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
        log.debug('connecting to %s', server_ip)
        self.connect( (server_ip, server_port) )

    def doloop(self):
        asyncore.loop(timeout=1, count=1)
        if self.need_stop:
            self.close()
            self.is_closed = True
            return
        if(time.time() - self.last_active > 45):
            log.debug('sending keep alive heart ...')
            self.ConfirmAlive()
            self.last_active = time.time()

    def reconnect(self):
        self.close()
        self.create_socket(AF_INET, SOCK_STREAM)
        log.debug('reconnecting to %s', self.server_ip)
        self.is_closed = False
        self.connect(self.server_ip, self.server_port)

    def disconnect(self):
        self.need_stop = True
        log.debug('disconnect to %s', self.server_ip)

    def handle_connect(self):
        log.debug('connect to server %s success', self.server_ip)

    def handle_close(self):
        self.close()
        self.is_closed = True
        log.info('server %s closed', self.server_ip)

    def handle_read(self):
        self.last_active = time.time()
        log.debug('begin read data from netmsgbus server')
        self.ReceivePack()

    def readable(self):
        return True

    def writable(self):
        return (len(self.buffer) > 0)

    def handle_write(self):
        log.debug('begin write data to netmsgbus server')
        sent = self.send(self.buffer)
        self.buffer = self.buffer[sent:]
        self.last_active = time.time()

    def handle_error(self):
        self.close()
        self.is_closed = True
        log.error('netmsgbus server connection has error')
        
    def ReceivePack(self):
        head = MsgBusPackHead()
        log.debug('reading pack head : %d ', head.HeadSize())
        headbuffer = self.recv(head.HeadSize())
        if not head.UnPackHead(headbuffer):
            log.error('unpack head error.')
            return
        bodybuffer = self.recv(head.body_len)
        log.debug('received pack type : %d, len:%d ', head.body_type, head.body_len)
        self.rsp_handlers.get(head.body_type, kMsgBusBodyType.UNKNOWN_BODY)(bodybuffer)

    def HandleRspConfirmAlive(self, bodybuffer):
        rsp = MsgBusConfirmAliveRsp()
        rsp.UnPackBody(bodybuffer)
        if(rsp.ret_code != 0):
            print "".join('%#04x' % ord(c) for c in bodybuffer)
            log.error("confirm alive not confirmed. ret_code: %d", rsp.ret_code)

    def HandleRspRegister(self, bodybuffer):
        reg_rsp = MsgBusRegisterRsp()
        reg_rsp.UnPackBody(bodybuffer)
        if(reg_rsp.ret_code == 0):
            self.isreceiver_registered = True
            self.receiver_name = reg_rsp.service_name
            log.info('netmsgbus receiver register success : %s', self.receiver_name)
        else:
            self.isreceiver_registered = False;
            log.info('netmsgbus receiver register failed: %s', reg_rsp.service_name)

    def HandleRspUnRegister(self, bodybuffer):
        log.info('unregister rsp from server.')

    def HandleUnknown(self, bodybuffer):
        log.error('got unknown body from netmsgbus server.')

    def RegisterNetMsgBusReceiver(self, clientip, clientport, clientname, busy_state = kServerBusyState.LOW):
        self.receiver_ip = clientip
        self.receiver_port = clientport
        self.receiver_name = clientname
        self.busy_state = busy_state
        host = ClientHost()
        host.server_ip = ''
        if clientip != '' :
            host.server_ip = socket.inet_aton(clientip)
        host.server_port = clientport
        host.busy_state = busy_state

        if( not self.connected):
            log.info('netmsgbus server is not connecting. Register will be done after connected')

        reg_req = MsgBusRegisterReq() 
        reg_req.service_name = clientname.ljust(MAX_SERVICE_NAME, '\0')
        reg_req.service_host = host
        outbuffer = reg_req.PackData()
        #log.debug('Register pack len: %d', len(outbuffer))
        #print "".join('%#04x' % ord(c) for c in outbuffer)
        self.buffer = self.buffer + outbuffer 

    def ConfirmAlive(self):
        req = MsgBusConfirmAliveReq();
        req.alive_flag = 0;
        self.buffer = self.buffer + req.PackData();

class ServerConnectionRunner(threading.Thread):
    def __init__(self, servermgr):
        threading.Thread.__init__(self)
        self.servermgr = servermgr

    def run(self):
        while not self.servermgr.is_closed:
            self.servermgr.doloop()

test = NetMsgBusServerConnMgr('127.0.0.1', 19000, '', 9100, 'test.receiverclient_A')
bg = ServerConnectionRunner(test)
bg.start()
#thread.start_new_thread(asyncore.loop, ())
bg.join(50)
test.disconnect()
log.debug('stopping ...')
bg.join()

