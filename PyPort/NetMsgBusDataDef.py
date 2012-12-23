#! /usr/bin/env python
# -*- coding: utf8 -*-

import struct

MAX_SERVICE_NAME = 64
MAX_SERVICE_NAME_STR = '64'

class kServerBusyState :
    (LOW, MIDDLE, HIGH, UNAVAILABLE) = range(0, 4)

class kMsgBusBodyType:
    REQ_REGISTER                         = 0x010001
    REQ_UNREGISTER                       = 0x010002
    REQ_CONFIRM_ALIVE                    = 0x010003
    REQ_SENDMSG                          = 0x010004
    REQ_GETCLIENT                        = 0x010005

    RSP_REGISTER                         = 0x020001
    RSP_UNREGISTER                       = 0x020002
    RSP_CONFIRM_ALIVE                    = 0x020003
    RSP_SENDMSG                          = 0x020004
    RSP_GETCLIENT                        = 0x020005

    BODY_PBTYPE                          = 0x030001
    BODY_JSONTYPE                        = 0x030002

    UNKNOWN_BODY                         = 0xFFFFFF

class ClientHost:
    def __init__(self, ip = '', port = 0, busy_state = 0):
        self.server_ip = ip   # ip is binary string with network order, can be get using inet_aton
        self.server_port = port
        self.busy_state = busy_state
    def pack(self):
        return self.server_ip.ljust(4, '\0') + struct.pack('!Hi', self.server_port, self.busy_state)
    def unpack(self, data):
        self.server_ip = data[:4]
        (self.server_port, self.busy_state) = struct.unpack_from('!Hi', data, 4)
    def Size(self):
        return 4 + struct.calcsize('!Hi')

class MsgBusPackHead :
    def __init__(self, type = 0, msgbody_type = 0):
        self.magic = 0x66
        self.version = 0x0001
        self.msg_type = type # 0: request, 1: response, 2: notify
        self.body_type = msgbody_type
        self.body_len = 0

    def PackHead(self):
        return struct.pack('!BHBiI', self.magic, self.version, self.msg_type, self.body_type, self.body_len) 
    def UnPackHead(self, data):
        (self.magic, self.version, self.msg_type, self.body_type, self.body_len) = struct.unpack_from('!BHBiI', data, 0)
        if self.magic != 0x66 or self.version != 0x0001:
            return False
        return True
    def HeadSize(self):
        return struct.calcsize('!BHBiI')

class MsgBusPackHeadReq(MsgBusPackHead):
    def __init__(self, type):
        MsgBusPackHead.__init__(self, 0, type)

    def PackReqHead(self):
        return MsgBusPackHead.PackHead(self)

    def UnPackReqHead(self, data):
        MsgBusPackHead.UnPackHead(self, data)

    def ReqHeadSize(self):
        return self.HeadSize()

class MsgBusPackHeadRsp(MsgBusPackHead):
    def __init__(self, type):
        MsgBusPackHead.__init__(self, 1, type)

    def PackRspHead(self):
        return MsgBusPackHead.PackHead(self)

    def UnPackRspHead(self, data):
        MsgBusPackHead.UnPackHead(self, data)

    def RspHeadSize(self):
        return self.HeadSize()

class MsgBusRegisterReq(MsgBusPackHeadReq):
    def __init__(self):
        MsgBusPackHeadReq.__init__(self, kMsgBusBodyType.REQ_REGISTER)
        self.service_name = ''
        self.service_host = ClientHost()

    def PackBody(self):
        return struct.pack('!' + MAX_SERVICE_NAME_STR + 's', self.service_name.ljust(MAX_SERVICE_NAME, '\0')) + self.service_host.pack()

    def PackData(self):
        self.body_len = self.BodySize()
        return self.PackReqHead() + self.PackBody()

    def UnPackBody(self, data):
        (self.service_name, ) = struct.unpack_from('!' + MAX_SERVICE_NAME_STR + 's', data, 0)
        self.service_name = self.service_name.strip('\0')
        self.service_host.unpack(data[MAX_SERVICE_NAME:])

    def UnPackData(self, data):
        self.UnPackReqHead(data)
        UnPackBody(self, data[self.ReqHeadSize():])

    def BodySize(self):
        return MAX_SERVICE_NAME + self.service_host.Size()

class MsgBusRegisterRsp(MsgBusPackHeadRsp):
    def __init__(self):
        MsgBusPackHeadRsp.__init__(self, kMsgBusBodyType.RSP_REGISTER)
        self.ret_code = 0
        self.service_name = ''
        self.err_msg_len = 0
        self.err_msg = ''

    def PackBody(self):
        return struct.pack('!H' + MAX_SERVICE_NAME_STR + 'sH' + str(err_msg_len) + 's', self.ret_code,
                self.service_name, self.err_msg_len, self.err_msg)

    def PackData(self):
        self.body_len = self.BodySize()
        return MsgBusPackHeadRsp.PackRspHead(self) + PackBody(self)

    def UnPackBody(self, data):
        (self.ret_code, self.service_name, self.err_msg_len) = struct.unpack_from('!H' + MAX_SERVICE_NAME_STR + 'sH', data, 0)
        self.service_name = self.service_name.strip('\0')
        used_size = struct.calcsize('!H' + MAX_SERVICE_NAME_STR + 'sH')
        (self.err_msg, ) = struct.unpack_from('!' + str(self.err_msg_len) + 's', data, used_size)

    def UnPackData(self, data):
        self.UnPackRspHead(self, data)
        self.UnPackBody(data[self.RspHeadSize():])

    def BodySize(self):
        return struct.calcsize('!H' + MAX_SERVICE_NAME_STR + 'sH' + str(err_msg_len) + 's')

    def GetErrMsg(self):
        return self.err_msg

    def SetErr(self, err_msg):
        if isinstance(err_msg, str):
            self.err_msg = err_msg
        elif isinstance(err_msg, unicode):
            self.err_msg = err_msg.encode('utf-8')
        else:
            raise "err_msg is not string type"
        self.err_msg_len = len(self.err_msg)

class MsgBusUnRegisterReq(MsgBusPackHeadReq):
    def __init__(self):
        MsgBusPackHeadReq.__init__(self, kMsgBusBodyType.REQ_UNREGISTER)
        self.service_name = ''
        self.service_host = ClientHost()

    def PackBody(self):
        return struct.pack('!' + MAX_SERVICE_NAME_STR + 's', self.service_name) + self.service_host.pack()

    def PackData(self):
        self.body_len = self.BodySize()
        return self.PackReqHead() + self.PackBody()

    def UnPackBody(self, data):
        (self.service_name, ) = struct.unpack_from('!' + MAX_SERVICE_NAME_STR + 's', data, 0)
        self.service_name = self.service_name.strip('\0')
        self.service_host.unpack(data[MAX_SERVICE_NAME:])

    def UnPackData(self, data):
        self.UnPackReqHead(data)
        self.UnPackBody(data[self.ReqHeadSize():])

    def BodySize(self):
        return struct.calcsize('!' + MAX_SERVICE_NAME_STR + 's') + self.service_host.Size()

class MsgBusConfirmAliveReq(MsgBusPackHeadReq):
    def __init__(self):
        MsgBusPackHeadReq.__init__(self, kMsgBusBodyType.REQ_CONFIRM_ALIVE)
        self.alive_flag = 0

    def PackBody(self):
        return struct.pack('!B', self.alive_flag)

    def PackData(self):
        self.body_len = self.BodySize()
        return self.PackReqHead() + self.PackBody()

    def UnPackBody(self, data):
        (self.alive_flag, ) = struct.unpack_from('!B', data, 0)

    def UnPackData(self, data):
        self.UnPackReqHead(data)
        self.UnPackBody(data[self.ReqHeadSize():])

    def BodySize(self):
        return struct.calcsize('!B')

class MsgBusConfirmAliveRsp(MsgBusPackHeadRsp):
    def __init__(self):
        MsgBusPackHeadRsp.__init__(self, kMsgBusBodyType.RSP_CONFIRM_ALIVE)
        self.ret_code = 0

    def PackBody(self):
        return struct.pack('!H', self.ret_code)

    def PackData(self):
        self.body_len = self.BodySize()
        return self.PackRspHead() + self.PackBody()

    def UnPackBody(self, data):
        (self.ret_code, ) = struct.unpack_from('!H', data, 0)

    def UnPackData(self, data):
        self.UnPackRspHead(data)
        self.UnPackBody(data[self.RspHeadSize():])

    def BodySize(self):
        return struct.calcsize('!H')

class MsgBusGetClientReq(MsgBusPackHeadReq):
    def __init__(self):
        MsgBusPackHeadReq.__init__(self, kMsgBusBodyType.REQ_GETCLIENT)
        self.dest_name = ''

    def PackBody(self):
        return struct.pack('!' + MAX_SERVICE_NAME_STR + 's', self.dest_name)

    def PackData(self):
        self.body_len = self.BodySize()
        return self.PackReqHead() + self.PackBody()

    def UnPackBody(self, data):
        (self.dest_name, ) = struct.unpack_from('!' + MAX_SERVICE_NAME_STR + 's', data, 0)
        self.dest_name = self.dest_name.strip('\0')

    def UnPackData(self, data):
        self.UnPackReqHead(data)
        self.UnPackBody(data[self.ReqHeadSize():])

    def BodySize(self):
        return struct.calcsize('!' + MAX_SERVICE_NAME_STR + 's')

class MsgBusGetClientRsp(MsgBusPackHeadRsp):
    def __init__(self):
        MsgBusPackHeadRsp.__init__(self, kMsgBusBodyType.RSP_GETCLIENT)
        self.ret_code = 0
        self.dest_name = ''
        self.dest_host = ClientHost()

    def PackBody(self):
        return struct.pack('!H' + MAX_SERVICE_NAME_STR + 's', self.ret_code, self.dest_name) + self.dest_host.pack()

    def PackData(self):
        self.body_len = self.BodySize()
        return self.PackRspHead() + self.PackBody()

    def UnPackBody(self, data):
        (self.ret_code, self.dest_name) = struct.unpack_from('!H' + MAX_SERVICE_NAME_STR + 's', data, 0)
        self.dest_name = self.dest_name.strip('\0')
        used_size = struct.calcsize('!H' + MAX_SERVICE_NAME_STR + 's')
        self.dest_host.unpack(data[used_size:])

    def UnPackData(self, data):
        self.UnPackRspHead(data)
        self.UnPackBody(data[self.RspHeadSize():])

    def BodySize(self):
        return struct.calcsize('!H' + MAX_SERVICE_NAME_STR + 's') + self.dest_host.Size()

class MsgBusSendMsgReq(MsgBusPackHeadReq):
    def __init__(self):
        MsgBusPackHeadReq.__init__(self, kMsgBusBodyType.REQ_SENDMSG)
        self.dest_name = ''
        self.from_name = ''
        self.msg_id = 0
        self.msg_len = 0
        self.msg_content = ''

    def PackBody(self):
        return struct.pack('!' + MAX_SERVICE_NAME_STR + 's' + MAX_SERVICE_NAME_STR + 'sII' + str(self.msg_len) + 's',
                self.dest_name, self.from_name, self.msg_id, self.msg_len, self.msg_content)

    def PackData(self):
        self.body_len = self.BodySize()
        return self.PackReqHead() + self.PackBody()

    def UnPackBody(self, data):
        (self.dest_name, self.from_name, self.msg_id, self.msg_len) = struct.unpack_from(
                '!' + MAX_SERVICE_NAME_STR + 's' + MAX_SERVICE_NAME_STR + 'sII', data, 0)
        self.dest_name = self.dest_name.strip('\0')
        self.from_name = self.from_name.strip('\0')
        used_size = struct.calcsize('!' + MAX_SERVICE_NAME_STR + 's' + MAX_SERVICE_NAME_STR + 'sII')
        (self.msg_content, ) = struct.unpack_from( '!' + str(self.msg_len) + 's', data, used_size)

    def UnPackData(self, data):
        self.UnPackReqHead(data)
        self.UnPackBody(data[self.ReqHeadSize():])

    def BodySize(self):
        return struct.calcsize('!' + MAX_SERVICE_NAME_STR + 's' +
                MAX_SERVICE_NAME_STR + 'sII' + str(self.msg_len) + 's')

    def GetMsgContent(self):
        return self.msg_content

    def SetMsgContent(self, content):
        if isinstance(content, unicode):
            self.msg_content = content.encode('utf-8')
        elif isinstance(content, str):
            self.msg_content = content
        else:
            raise 'msg_content is not string type'
        self.msg_len = len(self.msg_content)

    #char dest_name[MAX_SERVICE_NAME];
    #char from_name[MAX_SERVICE_NAME];
    #uint32_t msg_id;
    #uint32_t msg_len;
    #char * msg_content;

class MsgBusSendMsgRsp(MsgBusPackHeadRsp):
    def __init__(self):
        MsgBusPackHeadRsp.__init__(self, kMsgBusBodyType.RSP_SENDMSG)
        self.ret_code = 0
        self.msg_id = 0
        self.err_msg_len = 0
        self.err_msg = ''

    def PackBody(self):
        return struct.pack('!HIH' + str(self.err_msg_len) + 's', self.ret_code, self.msg_id, self.err_msg_len, self.err_msg)

    def PackData(self):
        self.body_len = self.BodySize()
        return self.PackRspHead() + self.PackBody()

    def UnPackBody(self, data):
        (self.ret_code, self.msg_id, self.err_msg_len) = struct.unpack_from('!HIH', data, 0)
        used_size = struct.calcsize('!HIH')
        (self.err_msg, ) = struct.unpack_from('!' + str(self.err_msg_len) + 's', data, used_size)

    def UnPackData(self, data):
        self.UnPackRspHead(data)
        self.UnPackBody(data[self.RspHeadSize():])

    def BodySize(self):
        return struct.calcsize('!HIH' + str(self.err_msg_len) + 's')

    def GetErrMsg(self):
        return self.err_msg

    def SetErr(self, err_msg):
        if isinstance(err_msg, unicode):
            self.err_msg = err_msg.encode('utf-8')
        elif isinstance(err_msg, str):
            self.err_msg = err_msg
        else:
            raise 'error message not string type!'
        self.err_msg_len = len(self.err_msg)

    #uint16_t ret_code;
    #uint32_t msg_id;
    #uint16_t err_msg_len;
    #char * err_msg;

class MsgBusPackPBType(MsgBusPackHead):
    def __init__(self):
        MsgBusPackHead.__init__(self, 0, kMsgBusBodyType.BODY_PBTYPE)
        self.pbtype_len = 0
        self.pbdata_len = 0
        self.pbtype = ''
        self.pbdata = ''

    def PackBody(self):
        return struct.pack('!ii' + str(self.pbtype_len) + 's' + str(self.pbdata_len) + 's', self.pbtype_len,
                self.pbdata_len, self.pbtype, self.pbdata)

    def PackData(self):
        self.body_len = self.BodySize()
        return self.PackHead() + self.PackBody()

    def UnPackBody(self, data):
        (self.pbtype_len, self.pbdata_len) = struct.unpack_from('!ii', data, 0)
        used_size = struct.calcsize('!ii')
        (self.pbtype, self.pbdata) = struct.unpack_from('!' + str(self.pbtype_len) + 's' + str(self.pbdata_len) + 's', data, used_size)
        self.pbtype = self.pbtype.rstrip('\0')

    def UnPackData(self, data):
        self.UnPackHead(data)
        self.UnPackBody(data[self.HeadSize():])

    def BodySize(self):
        return struct.calcsize('!ii' + str(self.pbtype_len) + 's' + str(self.pbdata_len) + 's')

    def GetPBType(self):
        return self.pbtype

    def GetPBData(self):
        return self.pbdata

    def SetPBTypeAndData(self, var_pbtype, var_pbdata):
        if isinstance(var_pbtype, str):
            self.pbtype = var_pbtype
        elif isinstance(var_pbtype, unicode):
            self.pbtype = var_pbtype.encode('utf-8')
        else:
            raise 'pbtype not string type'

        if isinstance(var_pbdata, str):
            self.pbdata = var_pbdata
        elif isinstance(var_pbdata, unicode):
            self.pbdata = var_pbdata.encode('utf-8')
        else:
            raise 'pbdata is not string type'

        self.pbtype_len = len(self.pbtype)
        self.pbdata_len = len(self.pbdata)

    #int32_t   pbtype_len;
    #int32_t   pbdata_len;
    #char*     pbtype;
    #char*     pbdata;



class ReceiverMsgUtil:
    @staticmethod
    def EncodeMsgKeyValue(orig_value):
        # replace % and &
        return orig_value.replace('%', '%25').replace('&', '%26')

    @staticmethod
    def DecodeMsgKeyValue(orig_value):
        # revert % and &
        return orig_value.replace('%26', '&').replace('%25', '%')

    @staticmethod
    def GetMsgKey(msgcontent, msgkey):
        msgvalue = ''
        startpos = msgcontent.find(msgkey + '=')
        if startpos != -1:
            endpos = msgcontent.find('&', startpos)
            if endpos != -1:
                msgvalue = msgcontent[startpos + len(msgkey) + 1:endpos]
            else:
                msgvalue = msgcontent[startpos + len(msgkey) + 1:]
        return ReceiverMsgUtil.DecodeMsgKeyValue(msgvalue)

    @staticmethod
    def GetMsgSender(msgcontent):
        return ReceiverMsgUtil.GetMsgKey(msgcontent, "msgsender")

    @staticmethod
    def GetMsgId(msgcontent):
        return ReceiverMsgUtil.GetMsgKey(msgcontent, "msgid")

    @staticmethod
    def GetMsgParam(msgcontent):
        return ReceiverMsgUtil.GetMsgKey(msgcontent, "msgparam")

    @staticmethod
    def MakeMsgNetData(msgid, msgparam, msgsender = ''):
        msgid = ReceiverMsgUtil.EncodeMsgKeyValue(msgid)
        msgparam = ReceiverMsgUtil.EncodeMsgKeyValue(msgparam)
        msgsender = ReceiverMsgUtil.EncodeMsgKeyValue(msgsender)
        netmsg_str = 'msgid=' + msgid + '&msgparam=' + msgparam
        if len(msgsender) == 0 :
            return netmsg_str
        return netmsg_str + '&msgsender=' + msgsender + '\0'

class ReceiverSendMsgReq:
    def __init__(self):
        self.is_sync = 0
        self.sync_sid = 0 # uint32_t
        self.data_len = 0 # uint32_t
        self.data = ''

    def PackData(self):
        return struct.pack('!BII' + str(self.data_len) + 's', self.is_sync, self.sync_sid, self.data_len, self.data)

    def UnPackHead(self, data):
        (self.is_sync, self.sync_sid, self.data_len) = struct.unpack_from('!BII', data, 0)

    def UnPackBody(self, data):
        (self.data, ) = struct.unpack_from('!' + str(self.data_len) + 's', data, 0)

    def UnPackData(self, data):
        self.UnPackHead(data)
        self.UnPackBody(data[self.HeadSize():])

    def HeadSize(self):
        return struct.calcsize('!BII')

    def SetMsgData(self, data):
        self.data_len = len(data)
        self.data = data

class ReceiverSendMsgRsp:
    def __init__(self):
        self.sync_sid = 0 # uint32_t
        self.data_len = 0 # uint32_t
        self.data = ''

    def PackData(self):
        return struct.pack('!II' + str(self.data_len) + 's', self.sync_sid, self.data_len, self.data)

    def UnPackHead(self, data):
        (self.sync_sid, self.data_len) = struct.unpack_from('!II', data, 0)

    def UnPackBody(self, data):
        (self.data, ) = struct.unpack_from('!' + str(self.data_len) + 's', data, 0)

    def UnPackData(self, data):
        self.UnPackHead(data)
        self.UnPackBody(data[self.HeadSize():])

    def HeadSize(self):
        return struct.calcsize('!II')

    def SetRspData(self, data):
        self.data_len = len(data)
        self.data = data

def ClientHostIsEqual(left, right):
    return (left.server_ip == right.server_ip) and (left.server_port == right.server_port)

