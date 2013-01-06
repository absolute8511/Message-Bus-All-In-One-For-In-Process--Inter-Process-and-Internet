#! /usr/bin/env python
# -*- coding: utf8 -*- 

import logging
from NetMsgBusServerConnMgr import *
from NetMsgBusReceiverMgr import *
from NetMsgBusReq2ReceiverMgr import *
import LocalMsgBus

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

class kMsgSendType:
    SendDirectToClient = 1
    SendUseServerRelay = 2

class NetMsgBus:
    server_conn_map = {}
    receiver_conn_map = {}
    serverconn_mgr = None
    req2receiver_mgr = None
    receiver_mgr = None
    serverconn_bg = None
    receiver_bg = None
    req2receiver_bg = None

    @staticmethod
    def Init(serverip, serverport, receiverip, receiverport, local_clientname):
        NetMsgBus.serverconn_mgr = NetMsgBusServerConnMgr(serverip, serverport, receiverip, receiverport, local_clientname, NetMsgBus.server_conn_map)
        NetMsgBus.serverconn_bg = ServerConnectionRunner(NetMsgBus.serverconn_mgr)
        NetMsgBus.serverconn_bg.daemon = True

        NetMsgBus.req2receiver_mgr = NetMsgBusReq2ReceiverMgr(NetMsgBus.serverconn_mgr)
        NetMsgBus.req2receiver_bg = Req2ReceiverMgrRunner(NetMsgBus.req2receiver_mgr)
        NetMsgBus.req2receiver_bg.daemon = True

        NetMsgBus.serverconn_bg.start()
        NetMsgBus.req2receiver_bg.start()

    @staticmethod
    def StartNetMsgBusReceiver(receiverip, receiverport, local_clientname):
        if receiverport > 0:
            NetMsgBus.receiver_mgr = NetMsgBusReceiverMgr(receiverip, receiverport, NetMsgBus.receiver_conn_map)
            NetMsgBus.receiver_bg = ReceiverMgrServerRunner(NetMsgBus.receiver_mgr)
            NetMsgBus.receiver_bg.daemon = True
            NetMsgBus.receiver_bg.start()

        ret = NetMsgBus.serverconn_mgr.RegisterNetMsgBusReceiver(receiverip, receiverport, local_clientname)
        if ret:
            log.info('register receiver success!!')
        else:
            log.info('register receiver failed. So receiver can only used for ip:port.')

    @staticmethod
    def DisConnectFromServer():
        NetMsgBus.serverconn_mgr.disconnect()

    @staticmethod
    def Destroy():
        NetMsgBus.serverconn_mgr.UnRegisterNetMsgBusReceiver()
        NetMsgBus.receiver_mgr.StopReceiver()
        NetMsgBus.serverconn_mgr.disconnect()
        NetMsgBus.req2receiver_bg.stop()
        log.debug('waiting to quit netmsgbus ...')
        NetMsgBus.serverconn_bg.join()
        NetMsgBus.receiver_bg.join()
        NetMsgBus.req2receiver_bg.join()

    @staticmethod
    def Wait(timeout):
        NetMsgBus.req2receiver_bg.join(timeout)

    @staticmethod
    def UpdateReceiverState(busy_state):
        return NetMsgBus.serverconn_mgr.UpdateReceiverState(busy_state)

    @staticmethod
    def NetQueryHostInfo(dest_name):
        return NetMsgBus.serverconn_mgr.ReqReceiverInfo(dest_name)

    @staticmethod
    def NetQueryAvailableServices(match_msgstr):
        return NetMsgBus.serverconn_mgr.QueryAvailableServices(match_msgstr)

    @staticmethod
    def NetSendMsg(destname_or_ipport, msgid, msgdata, sendtype):
        netmsg_data = ReceiverMsgUtil.MakeMsgNetData(msgid, msgdata)
        if sendtype == kMsgSendType.SendUseServerRelay:
            return NetMsgBus.serverconn_mgr.PostNetMsgUseServerRelay(destname_or_ipport, netmsg_data)
        elif sendtype == kMsgSendType.SendDirectToClient:
            return NetMsgBus.req2receiver_mgr.PostMsgDirectToClient(destname_or_ipport, netmsg_data)
        else:
            log.warn('not supported sendtype : %d in NetSendMsg.', sendtype)
            return False

    @staticmethod
    def NetAsyncGetData(destname_or_ipport, msgid, msgdata, callback):
        netmsg_data = ReceiverMsgUtil.MakeMsgNetData(msgid, msgdata)
        return NetMsgBus.req2receiver_mgr.PostMsgDirectToClient(destname_or_ipport, netmsg_data, callback)

    # return (True, rsp), True for success, False for timeout or error.
    @staticmethod
    def NetSyncGetData(destname_or_ipport, msgid, msgdata, timeout_sec = 30):
        netmsg_data = ReceiverMsgUtil.MakeMsgNetData(msgid, msgdata)
        return NetMsgBus.req2receiver_mgr.SendMsgDirectToClient(destname_or_ipport, netmsg_data, timeout_sec)

