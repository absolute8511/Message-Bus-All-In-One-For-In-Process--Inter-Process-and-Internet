# $< stand for the first file of prerequirefiles
# $@ stand for the target files
# $^ stand for all of the prerequirefiles
# %.o:%.cpp stand for the files which replace the %.o's suffix with .cpp
# use -gstabs+ or -gdwarf-2 to get more debug info ??
CC := g++
AR := ar
BINDIR := $(MAKEROOT)/Bin
LIBDIR := $(MAKEROOT)/Libs
OBJDIR := $(MAKEROOT)/Objs
SHELLROOT := $(shell pwd)

PBCC := protoc
PBCCFLAGS := --cpp_out=.
ifeq ($(BUILD),release)
# for release version
CPPFLAGS :=  -O3 -g -DNDEBUG -D__STDC_FORMAT_MACROS -fPIC -I$(BOOST_ROOT)
else
# for debug version
CPPFLAGS :=  -O0  -D__STDC_FORMAT_MACROS -Wall -fPIC -g -pg -I$(BOOST_ROOT)
endif
LDFLAGS := -L. -L$(BINDIR) -L$(LIBDIR) -Wl,-rpath,./ -Wl,-rpath-link,$(SHELLROOT)/$(BINDIR):$(SHELLROOT)/$(LIBDIR)
SHARED  := -shared
ARFLAGS := crs

all:

.PHONY: all clean

release:
	make "BUILD=release"

$(OBJDIR)/%.pb.o:%.pb.cc
	$(CC) -c $(CPPFLAGS) `pkg-config --cflags protobuf` $< -o $@

$(OBJDIR)/%.o:%.cpp
	$(CC) -c $(CPPFLAGS) $< -o $@

%.pb.cc : %.proto
	$(PBCC) $(PBCCFLAGS) $<

%.pb.h : %.proto
	$(PBCC) $(PBCCFLAGS) $<

include $(SRCFILES:.cpp=.d)
include $(PBSRCFILES:.pb.cc=.pb.d)

%.d: %.cpp
	@set -e;rm -f $@; $(CC) -MM $(CPPFLAGS) $< > $@.$$$$; sed 's,\($*\)\.o[ :]*,$(OBJDIR)/\1.o $@ : ,g' < $@.$$$$ > $@; rm -f $@.$$$$

%.pb.d: %.pb.cc
	@set -e;rm -f $@;\
	   	$(CC) -MM $(CPPFLAGS) $< > $@.$$$$;\
	   	sed 's,\($*\)\.o[ :]*,$(OBJDIR)/\1.o $@ : ,g' < $@.$$$$ > $@;\
		rm -f $@.$$$$


