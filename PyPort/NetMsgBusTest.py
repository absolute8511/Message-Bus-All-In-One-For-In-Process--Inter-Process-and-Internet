#!/usr/bin/env python
# -*- coding: utf8 -*-

import LocalMsgBus
from msgbus_client import *

def test_localmsg_handler(msgid, msgparam):
    print 'handler in test local msgbus: param' + msgparam
    return ('retparam', True)

def test_future_call_back(future):
    if future.ready:
        log.info('callback from future, ready for rsp: %s', future.rsp)
    else:
        log.info('callback from future, failed to get rsp')

class TestHandler:
    def OnMsg(self, msgid, msgparam):
        print 'handle in OnMsg object'
        return test_localmsg_handler(msgid, msgparam)
    @staticmethod
    def StaticOnMsg(msgid, msgparam):
        print 'handler in TestHandler static onmsg'
        return test_localmsg_handler(msgid, msgparam)


class ConcurrentTest(threading.Thread):
    def __init__(self):
        threading.Thread.__init__(self)

    def run(self):
        times = 10000
        while times > 0:
            times = times - 1;
            rsp = NetMsgBus.NetSyncGetData(('127.0.0.1', 9101), 'msg_netmsgbus_testmsg1', '{"testkey":11111, "testlongdata": "frompythondata"}', 3)
            if rsp[0] and rsp[1]:
                pass
                #print 'sync get data from receiver: ' + rsp[1]
            else:
                print 'sync get data from receiver failed.'

LocalMsgBus.InitMsgBus()

test_handler = TestHandler()
LocalMsgBus.AddHandler('msg_netmsgbus_testgetdata', test_localmsg_handler)
LocalMsgBus.AddHandler('msg_netmsgbus_testgetdata', test_handler)
# test non-staticmethod add will fail
LocalMsgBus.AddHandler('msg_netmsgbus_testgetdata', test_handler.OnMsg)
LocalMsgBus.AddHandler('msg_netmsgbus_testgetdata', TestHandler.StaticOnMsg)

NetMsgBus.Init('127.0.0.1', 19000, '', 9100, 'test.receiverclient_A')
NetMsgBus.Wait(2)
NetMsgBus.StartNetMsgBusReceiver('', 9100, 'test.receiverclient_A')
# test for server connection
# broadcast message.
ret = NetMsgBus.NetSendMsg('', 'test.broadcast.msgtestid', '123testserver_broadcast', kMsgSendType.SendUseServerRelay)
if ret:
    print "send using server relay broadcast success"
else:
    print "send using server relay broadcast failed"
NetMsgBus.Wait(2)
# test send group message
NetMsgBus.NetSendMsg('test.', 'test.group.msgtestid', '123testserver_group', kMsgSendType.SendUseServerRelay)
if ret:
    print "send using server relay group success"
else:
    print "send using server relay group failed"
NetMsgBus.Wait(2)
NetMsgBus.NetSendMsg('test.receiverclient_B', 'test.postmsg', '123testserver_relay', kMsgSendType.SendUseServerRelay)
if ret:
    print "send using server relay to B success"
else:
    print "send using server relay to B failed"
NetMsgBus.Wait(2)
rethost = NetMsgBus.NetQueryHostInfo('test.receiverclient_B')
if rethost:
    print "query host return : "
    print rethost
NetMsgBus.Wait(2)
ret = NetMsgBus.NetQueryAvailableServices('test.receiverclient')
if ret:
    print "query available services : " + ret
NetMsgBus.Wait(2)
NetMsgBus.UpdateReceiverState(kServerBusyState.MIDDLE)
NetMsgBus.Wait(2)

# test for req2receivermgr
rsp = NetMsgBus.NetSyncGetData(('127.0.0.1', 9101), 'msg_netmsgbus_testmsg1', '{"testkey":11111, "testlongdata": "frompythondata"}', 5)
if rsp[0] and rsp[1]:
    print 'sync get data from receiver: ' + rsp[1]
else:
    print 'sync get data from receiver failed'
rsp = NetMsgBus.NetSyncGetData('test.receiverclient_B', 'msg_netmsgbus_testmsg1', '{"testkey":11113, "testlongdata": "frompythondata"}', 5)
if rsp[0] and rsp[1]:
    print 'sync get data using name : ' + rsp[1]
else:
    print 'sync get data from receiver using name failed'

future = NetMsgBus.NetAsyncGetData(('127.0.0.1', 9101), 'msg_netmsgbus_testmsg1', '{"testkey":11112, "testlongdata": "frompythondata"}', test_future_call_back)
if future.get(3):
    print 'async get data from receiver: ' + future.get(3)
else:
    print 'async get data from receiver failed'

future = NetMsgBus.NetAsyncGetData('test.receiverclient_B', 'msg_netmsgbus_testmsg1', '{"testkey":11113, "testlongdata": "frompythondata"}', test_future_call_back)
if future.get(5):
    print 'async get data using name : ' + future.get(5)
else:
    print 'async get data from receiver using name failed'

log.debug('begin ConcurrentTest')
concurrent_tests = []
for i in range(10):
    concurrent_tests.append(ConcurrentTest())
    concurrent_tests[i].daemon = True

for i in range(10):
    concurrent_tests[i].start()
# wait for test receiver server, wait for data from other client. and test for long no active
for i in range(10):
    concurrent_tests[i].join()

log.debug('concurrent_tests finished.')
log.debug('waiting 30s to quit ...')
NetMsgBus.Wait(30)
NetMsgBus.Destroy()

LocalMsgBus.DestroyMsgBus()
