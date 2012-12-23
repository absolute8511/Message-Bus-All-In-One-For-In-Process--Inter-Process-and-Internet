#! /usr/bin/env python
# -*- coding: utf8 -*- 

import string
import threading
import asyncore, socket
import time
import logging
from NetMsgBusDataDef import *
import LocalMsgBus
from NetMsgBus import *  # for protobuf Type

logging.basicConfig(level=logging.DEBUG, format="%(created)-15s %(msecs)d %(levelname)8s %(thread)d %(name)s %(message)s")
log = logging.getLogger(__name__)

class NetMsgBusServerConnMgr(asyncore.dispatcher):
    def __init__(self, server_ip, server_port, receiver_ip, receiver_port, receiver_name, sockmap):
        asyncore.dispatcher.__init__(self, map=sockmap)
        self.rsp_handlers = {
                kMsgBusBodyType.RSP_CONFIRM_ALIVE : self.HandleRspConfirmAlive,
                kMsgBusBodyType.RSP_REGISTER : self.HandleRspRegister,
                kMsgBusBodyType.RSP_UNREGISTER : self.HandleRspUnRegister,
                kMsgBusBodyType.RSP_GETCLIENT : self.HandleRspGetClient,
                kMsgBusBodyType.RSP_SENDMSG : self.HandleRspSendMsg,
                kMsgBusBodyType.REQ_SENDMSG : self.HandleReqSendMsg,
                kMsgBusBodyType.BODY_PBTYPE : self.HandleRspPBBody,
                kMsgBusBodyType.UNKNOWN_BODY : self.HandleUnknown
                }

        self.pbdata_handlers = {
                PBParam_pb2.PBQueryServicesRsp.DESCRIPTOR.full_name : self.HandlePBQueryServicesRsp,
                'unknown_pbtype' : self.HandlePBUnknown
                }

        self.sockmap = sockmap
        self.writelocker = threading.Lock()
        self.server_ip = server_ip
        self.server_port = server_port
        self.buffer = ''
        self.read_buffer = ''
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
        asyncore.loop(timeout=5, count=1, map=self.sockmap)
        if self.need_stop:
            self.close()
            self.is_closed = True
            return
        if(time.time() - self.last_active > 45):
            log.info('sending keep alive heart ...')
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
        self.ReceivePack()

    def readable(self):
        return True

    def writable(self):
        return (len(self.buffer) > 0)

    def handle_write(self):
        with self.writelocker:
            sent = self.send(self.buffer)
            self.buffer = self.buffer[sent:]
        self.last_active = time.time()

    def handle_error(self):
        self.close()
        self.is_closed = True
        log.error('!!!!netmsgbus server connection has error!!!!!')
        
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
            head = MsgBusPackHead()
            if len(self.read_buffer) < head.HeadSize():
                return
            headbuffer = self.read_buffer[:head.HeadSize()]
            if not head.UnPackHead(headbuffer):
                log.error('unpack head error.')
                self.read_buffer = ''
                return
            if len(self.read_buffer) < head.HeadSize() + head.body_len:
                return
            bodybuffer = self.read_buffer[head.HeadSize():head.body_len + head.HeadSize()]
            self.read_buffer = self.read_buffer[head.body_len + head.HeadSize():]
            log.debug('received pack type : %d, len:%d ', head.body_type, head.body_len)
            self.rsp_handlers.get(head.body_type, self.HandleUnknown)(bodybuffer)

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

    def HandleRspGetClient(self, bodybuffer):
        rsp = MsgBusGetClientRsp()
        rsp.UnPackBody(bodybuffer)
        if rsp.ret_code == 0:
            dest_ip = socket.inet_ntoa(rsp.dest_host.server_ip)
            dest_port = rsp.dest_host.server_port
            dest_clientname = rsp.dest_name
            log.info('get client info returned. ret name: %s, ip:port : %s:%d', dest_clientname, dest_ip, dest_port)
            LocalMsgBus.SendMsg('netmsg.sever.rsp.getclient', (rsp.ret_code, dest_clientname, (dest_ip, dest_port)))
        else:
            log.info('msgbus server return error while get client info, ret_code: %d.', rsp.ret_code)

    def HandleRspSendMsg(self, bodybuffer):
        #本客户端通过服务器向其他客户端转发消息得到的服务器返回确认
        rsp = MsgBusSendMsgRsp()
        rsp.UnPackBody(bodybuffer)
        if rsp.ret_code == 0:
            log.debug('send message using server relay return success.')
        else:
            log.info('send msg by server error: %d, errmsg: %s.', rsp.ret_code, rsp.GetErrMsg())
        
    def HandleReqSendMsg(self, bodybuffer):
        #收到服务器转发的其他客户端的发消息请求
        req = MsgBusSendMsgReq()
        req.UnPackBody(bodybuffer)
        log.info('got message from server relay, from:%s, dest:%s, msgid:%d.', req.from_name, req.dest_name, req.msg_id)
        log.info('message content:%s.', req.GetMsgContent())
        # transfer netmsg_data to local msgbus data.
        # find msgid and msgparam and msgsender for local msgbus
        netmsgdata = req.GetMsgContent()
        msgid = ReceiverMsgUtil.GetMsgId(netmsgdata)
        if msgid == '':
            log.debug('empty msgid from server relay message.')
            return
        LocalMsgBus.SendMsg(msgid, ReceiverMsgUtil.GetMsgParam(netmsgdata));

    def HandleRspPBBody(self, bodybuffer):
        pbpack = MsgBusPackPBType()
        pbpack.UnPackBody(bodybuffer)
        log.debug('got pbrsp, pbtype:%s.', pbpack.GetPBType())
        self.pbdata_handlers.get(pbpack.GetPBType(), self.HandlePBUnknown)(pbpack.GetPBData())

    def HandleUnknown(self, bodybuffer):
        log.error('got unknown body from netmsgbus server.')

    def HandlePBQueryServicesRsp(self, pbdata):
        rsp = PBParam_pb2.PBQueryServicesRsp()
        rsp.ParseFromString(pbdata)
        print 'all available service matched are:' + ','.join(rsp.service_name)

    def HandlePBUnknown(self, pbdata):
        log.info('got unknown_pbtype, pbdata is : %s .', pbdata)

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

        if not self.connected:
            log.info('netmsgbus server is not connecting. Register will be done after connected')

        reg_req = MsgBusRegisterReq() 
        reg_req.service_name = clientname.ljust(MAX_SERVICE_NAME, '\0')
        reg_req.service_host = host
        #log.debug('Register pack len: %d', len(reg_req.PackData()))
        #print "".join('%#04x' % ord(c) for c in reg_req.PackData())
        with self.writelocker:
            self.buffer += reg_req.PackData() 
        if self.connected:
            self.handle_write()
        return True

    def UpdateReceiverState(self, busy_state):
        if self.receiver_port == 0 or self.receiver_name == '':
            return False
        return self.RegisterNetMsgBusReceiver(self.receiver_ip, self.receiver_port, self.receiver_name, busy_state)

    def UnRegisterNetMsgBusReceiver(self):
        if not self.connected:
            return False
        unreg_req = MsgBusUnRegisterReq()
        unreg_req.service_name = self.receiver_name.ljust(MAX_SERVICE_NAME, '\0')
        host = ClientHost()
        host.server_ip = ''
        if self.receiver_ip != "":
            host.server_ip = socket.inet_aton(self.receiver_ip)
        host.server_port = self.receiver_port
        unreg_req.service_host = host
        with self.writelocker:
            self.buffer += unreg_req.PackData()
        self.handle_write()
        self.isreceiver_registered = False
        return True

    def ConfirmAlive(self):
        if not self.connected:
            return False
        req = MsgBusConfirmAliveReq()
        req.alive_flag = 0
        with self.writelocker:
            self.buffer += req.PackData()
        self.handle_write()
        return True

    def PostNetMsgUseServerRelay(self, clientname, data):
        if not self.connected:
            return False
        sendmsg_req = MsgBusSendMsgReq()
        sendmsg_req.dest_name = clientname.ljust(MAX_SERVICE_NAME, '\0')
        sendmsg_req.from_name = self.receiver_name.ljust(MAX_SERVICE_NAME, '\0')
        sendmsg_req.msg_id = int(time.time())
        log.debug('server tick msgid %d, sendmsg use server relay from:%s, to:%s.', sendmsg_req.msg_id, sendmsg_req.from_name, sendmsg_req.dest_name);
        sendmsg_req.SetMsgContent(data)
        with self.writelocker:
            self.buffer += sendmsg_req.PackData()
        self.handle_write()
        return True

    def ReqReceiverInfo(self, clientname):
        if not self.connected:
            return False
        get_client_req = MsgBusGetClientReq()
        get_client_req.dest_name = clientname.ljust(MAX_SERVICE_NAME, '\0')
        with self.writelocker:
            self.buffer += get_client_req.PackData()
        self.handle_write()
        return True

    def QueryAvailableServices(self, match_str):
        if not self.connected:
            return False
        services_query = MsgBusPackPBType()
        pbreq = PBParam_pb2.PBQueryServicesReq()
        pbreq.match_prefix = match_str
        pbtype = PBParam_pb2.PBQueryServicesReq.DESCRIPTOR.full_name
        pbtype += '\0';
        services_query.SetPBTypeAndData(pbtype, pbreq.SerializeToString())
        log.debug('begin query all available services. %s, pbtype:%s, pbdata len:%d', match_str, pbtype, len(pbreq.SerializeToString()))
        with self.writelocker:
            self.buffer += services_query.PackData()
        self.handle_write()
        #print "".join('%#04x' % ord(c) for c in services_query.PackData())
        return True

class ServerConnectionRunner(threading.Thread):
    def __init__(self, servermgr):
        threading.Thread.__init__(self)
        self.servermgr = servermgr

    def run(self):
        while not self.servermgr.is_closed:
            self.servermgr.doloop()

