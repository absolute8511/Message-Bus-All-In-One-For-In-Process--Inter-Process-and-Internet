#! /usr/bin/env python
# -*- coding: utf8 -*- 

import string
import thread
from socket import *
import asyncore, socket
from time import sleep
import logging
from NetMsgBusDataDef import *

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

class NetMsgBusServerConnMgr(asyncore.dispatcher):

    def __init__(self, server_ip, server_port, receiver_ip, receiver_port, receiver_name):
        asyncore.dispatcher.__init__(self)
        self.server_ip = server_ip
        self.server_port = server_port
        self.is_connected = False;
        self.buffer = ''
        self.timeout = 45
        self.RegisterNetMsgBusReceiver(receiver_ip, receiver_port, receiver_name, kServerBusyState.LOW)
        self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
        log.debug('connecting to %s', server_ip)
        self.connect( (server_ip, server_port) )

    def doloop(self):
        asyncore.loop(45)

    def reconnect(self):
        self.close()
        self.is_connected = False;
        self.create_socket(AF_INET, SOCK_STREAM)
        log.debug('reconnecting to %s', self.server_ip)
        self.connect(self.server_ip, self.server_port)

    def disconnect(self):
        self.close()
        self.is_connected = False;
        log.debug('disconnect to %s', self.server_ip)

    def handle_connect(self):
        self.is_connected = True;
        log.debug('connect to server %s success', self.server_ip)

    def handle_close(self):
        self.close()
        self.is_connected = False;
        log.info('server %s closed', self.server_ip)

    def handle_read(self):
        log.debug('begin read data from netmsgbus server')
        print self.recv(8192)

    def readable(self):
        return True

    def writable(self):
        return (len(self.buffer) > 0)

    def handle_write(self):
        log.debug('begin write data to netmsgbus server')
        sent = self.send(self.buffer)
        self.buffer = self.buffer[sent:]

    def handle_error(self):
        self.close()
        self.is_connecting = False;
        log.error('netmsgbus server connection has error')
        
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

        if( not self.is_connected):
            log.info('netmsgbus server is not connecting. Register will be done after connected')

        reg_req = MsgBusRegisterReq() 
        reg_req.service_name = clientname.ljust(MAX_SERVICE_NAME, '\0')
        reg_req.service_host = host
        outbuffer = reg_req.PackData()
        log.debug('Register pack len: %d', len(outbuffer))
        print "".join('%#04x' % ord(c) for c in outbuffer)
        self.buffer = self.buffer + outbuffer 

test = NetMsgBusServerConnMgr('127.0.0.1', 19000, '', 9100, 'test.receiverclient_A')
asyncore.loop()
#thread.start_new_thread(asyncore.loop, ())

test.disconnect()


