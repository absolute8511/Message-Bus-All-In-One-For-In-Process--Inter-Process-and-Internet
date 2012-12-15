#! /usr/bin/env python
# -*- coding: utf8 -*- 

import threading
import time
from time import sleep
import logging
from NetMsgBusServerConnMgr import *
from NetMsgBusReceiverMgr import *
from LocalMsgBus import *
#from NetMsgBusInterface import *

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

def test_localmsg_handler(msgid, msgparam):
    print 'handler in test local msgbus: param' + msgparam
    return ('', True)

class TestHandler:
    def OnMsg(self, msgid, msgparam):
        print 'handle in OnMsg object'
        return test_localmsg_handler(msgid, msgparam)

test_handler = TestHandler()
LocalMsgBus.g_msgbus.AddHandler('msg_netmsgbus_testgetdata', test_localmsg_handler)
LocalMsgBus.g_msgbus.AddHandler('msg_netmsgbus_testgetdata', test_handler)

server_conn_map = {}
receiver_conn_map = {}
test = NetMsgBusServerConnMgr('127.0.0.1', 19000, '', 9100, 'test.receiverclient_A', server_conn_map)
receivermgr = NetMsgBusReceiverMgr('', 9100, receiver_conn_map)
bg = ServerConnectionRunner(test)
receiver_bg = ReceiverMgrServerRunner(receivermgr)
bg.daemon = True
receiver_bg.daemon = True
bg.start()
receiver_bg.start()
bg.join(2)
# test for server connection
test.ReqReceiverInfo('test.receiverclient_B')
bg.join(1)
test.PostNetMsgUseServerRelay('test.receiverclient_B', 'msgid=test.postmsg&msgparam=123')
bg.join(1)
test.QueryAvailableServices('')
bg.join(1)

# wait for test receiver server, wait for data from other client. and test for long no active
receiver_bg.join(90)
bg.join(5)
log.debug('unregister ...')
test.UnRegisterNetMsgBusReceiver()
receivermgr.StopReceiver()
bg.join(3)
test.disconnect()
log.debug('waiting to quit ...')
bg.join()
receiver_bg.join()

