# $< stand for the first file of prerequirefiles
# $@ stand for the target files
# $^ stand for all of the prerequirefiles
# %.o:%.cpp stand for the files which replace the %.o's suffix with .cpp
export MAKEROOT := ..

THREADPOOL_OBJS:= threadpool.o named_worker_thread.o threadpoolimp.o multitimer.o
MSGBUS_SERVER_OBJS := msgbus_server.o msgbus_def.o
MSGBUS_CLIENT_OBJS := msgbus_client.o msgbus_def.o msgbus_interface.o MsgHandlerMgr.o NetMsgBusFilterMgr.o
EVENTLOOPPOOL_OBJS := EventLoopPool.o EventLoop.o SockWaiterBase.o SelectWaiter.o TcpSock.o EpollWaiter.o
LOGGER_OBJS := SimpleLogger.o

export SRCFILES := EventLoop.cpp EventLoopPool.cpp msgbus_client.cpp msgbus_interface.cpp \
	msgbus_server.cpp SelectWaiter.cpp SockWaiterBase.cpp TcpSock.cpp threadpool.cpp \
	named_worker_thread.cpp threadpoolimp.cpp multitimer.cpp MsgHandlerMgr.cpp \
	NetMsgBusFilterMgr.cpp SimpleLogger.cpp EpollWaiter.cpp

include $(MAKEROOT)/template.mk

LDFLAGS := -lpthread -lrt -lthreadpool $(LDFLAGS) 

THREADPOOL_TARGET := $(BINDIR)/libthreadpool.so.3
THREADPOOL_OBJS_PATH := $(THREADPOOL_OBJS:%.o=$(OBJDIR)/%.o)
MSGBUS_SERVER_TARGET := $(BINDIR)/msgbus_server 
MSGBUS_CLIENT_TARGET := $(BINDIR)/libmsgbusclient.so.3
MSGBUS_SERVER_OBJS_PATH :=$(MSGBUS_SERVER_OBJS:%.o=$(OBJDIR)/%.o) 
MSGBUS_CLIENT_OBJS_PATH := $(MSGBUS_CLIENT_OBJS:%.o=$(OBJDIR)/%.o)
EVENTLOOPPOOL_OBJS_PATH := $(EVENTLOOPPOOL_OBJS:%.o=$(OBJDIR)/%.o)
LOGGER_OBJS_PATH := $(LOGGER_OBJS:%.o=$(OBJDIR)/%.o)
TESTTARGET := $(BINDIR)/test_client

all:$(THREADPOOL_TARGET) $(MSGBUS_CLIENT_TARGET) $(MSGBUS_SERVER_TARGET) 

msgbus:$(MSGBUS_CLIENT_TARGET) $(MSGBUS_SERVER_TARGET) 

threadpool:$(THREADPOOL_TARGET)

test_client:$(TESTTARGET)

$(MSGBUS_CLIENT_TARGET):$(MSGBUS_CLIENT_OBJS_PATH) $(EVENTLOOPPOOL_OBJS_PATH) $(LOGGER_OBJS_PATH)
	$(CC) $(SHARED) -o $@ $^ 

$(THREADPOOL_TARGET):$(THREADPOOL_OBJS_PATH)
	$(CC) $(SHARED)  -o $@ $^ 

$(MSGBUS_SERVER_TARGET): $(MSGBUS_SERVER_OBJS_PATH) $(EVENTLOOPPOOL_OBJS_PATH) $(LOGGER_OBJS_PATH)
	$(CC)  -o $@ $^ $(LDFLAGS) 

$(TESTTARGET):test.cpp msgbus_handlerbase.hpp msgbus_interface.h threadpool.h xparam.hpp MsgHandlerMgr.o
	$(CC) $(CPPFLAGS) -o $@ $< MsgHandlerMgr.o -lmsgbusclient -ljsoncpp $(LDFLAGS)  

clean:
	-rm -f *.d $(THREADPOOL_TARGET) $(THREADPOOL_OBJS_PATH) $(EVENTLOOPPOOL_OBJS_PATH) \
		$(MSGBUS_SERVER_TARGET) $(MSGBUS_CLIENT_TARGET) $(MSGBUS_CLIENT_OBJS_PATH) \
		$(MSGBUS_SERVER_OBJS_PATH) $(LOGGER_OBJS_PATH)

cleantest:
	-rm -f $(TESTTARGET)

.PHONY: cleantest

