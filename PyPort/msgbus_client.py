#! /usr/bin/env python
# -*- coding: utf8 -*- 

import threading
import time
from time import sleep
import logging
from NetMsgBusServerConnMgr import *
from NetMsgBusReceiverMgr import *
#from NetMsgBusInterface import *

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

servermap = {}
receivermap = {}
test = NetMsgBusServerConnMgr('127.0.0.1', 19000, '', 9100, 'test.receiverclient_A', servermap)
receivermgr = NetMsgBusReceiverMgr('', 9100, receivermap)
bg = ServerConnectionRunner(test)
receiver_bg = ReceiverMgrServerRunner(receivermgr)
bg.daemon = True
receiver_bg.daemon = True
bg.start()
receiver_bg.start()
bg.join(10)
test.ReqReceiverInfo('test.receiverclient_B')
#test.PostNetMsgUseServerRelay('test.receiverclient_B', 'msgid=test.postmsg&msgparam=123')
bg.join(5)
test.QueryAvailableServices('')
bg.join(30)
#log.debug('unregister ...')
#test.UnRegisterNetMsgBusReceiver()
bg.join(5)
test.disconnect()
receivermgr.StopReceiver()
log.debug('stopping ...')
bg.join(5)
receiver_bg.join(5)

