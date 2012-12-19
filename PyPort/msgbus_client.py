#! /usr/bin/env python
# -*- coding: utf8 -*- 

import logging
from NetMsgBusServerConnMgr import *
from NetMsgBusReceiverMgr import *
from NetMsgBusReq2ReceiverMgr import *
from LocalMsgBus import *
#from NetMsgBusInterface import *

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

def test_localmsg_handler(msgid, msgparam):
    print 'handler in test local msgbus: param' + msgparam
    return ('retparam', True)

class TestHandler:
    def OnMsg(self, msgid, msgparam):
        print 'handle in OnMsg object'
        return test_localmsg_handler(msgid, msgparam)
    @staticmethod
    def StaticOnMsg(msgid, msgparam):
        print 'handler in TestHandler static onmsg'
        return test_localmsg_handler(msgid, msgparam)


test_handler = TestHandler()
LocalMsgBus.AddHandler('msg_netmsgbus_testgetdata', test_localmsg_handler)
LocalMsgBus.AddHandler('msg_netmsgbus_testgetdata', test_handler)
LocalMsgBus.AddHandler('msg_netmsgbus_testgetdata', test_handler.OnMsg)
LocalMsgBus.AddHandler('msg_netmsgbus_testgetdata', TestHandler.StaticOnMsg)

server_conn_map = {}
receiver_conn_map = {}
serverconn_mgr = NetMsgBusServerConnMgr('127.0.0.1', 19000, '', 9100, 'test.receiverclient_A', server_conn_map)
receivermgr = NetMsgBusReceiverMgr('', 9100, receiver_conn_map)
req2receivermgr = NetMsgBusReq2ReceiverMgr(serverconn_mgr)

serverconn_bg = ServerConnectionRunner(serverconn_mgr)
receiver_bg = ReceiverMgrServerRunner(receivermgr)
req2receiver_bg = Req2ReceiverMgrRunner(req2receivermgr)

serverconn_bg.daemon = True
receiver_bg.daemon = True
req2receiver_bg.daemon = True
serverconn_bg.start()
receiver_bg.start()
req2receiver_bg.start()
serverconn_bg.join(2)
# test for server connection
serverconn_mgr.ReqReceiverInfo('test.receiverclient_B')
serverconn_bg.join(1)
serverconn_mgr.PostNetMsgUseServerRelay('test.receiverclient_B', 'msgid=test.postmsg&msgparam=123')
serverconn_bg.join(1)
serverconn_mgr.QueryAvailableServices('')
serverconn_bg.join(3)

# test for req2receivermgr
rsp = req2receivermgr.SendMsgDirectToClient(('127.0.0.1', 9101), 'msgid=msg_netmsgbus_testmsg1&msgparam=datafrompython', 3)
if rsp[0]:
    print 'sync get data from receiver: ' + rsp[1]
else:
    print 'sync get data from receiver failed'

future = req2receivermgr.PostMsgDirectToClient(('127.0.0.1', 9101), 'msgid=msg_netmsgbus_testmsg1&msgparam=datafrompython')
print 'async get data from receiver: ' + future.get(3)

# wait for test receiver server, wait for data from other client. and test for long no active
receiver_bg.join(30)
req2receiver_bg.stop()
log.debug('unregister ...')
serverconn_mgr.UnRegisterNetMsgBusReceiver()
receivermgr.StopReceiver()
serverconn_bg.join(3)
serverconn_mgr.disconnect()
log.debug('waiting to quit ...')
serverconn_bg.join()
receiver_bg.join()

