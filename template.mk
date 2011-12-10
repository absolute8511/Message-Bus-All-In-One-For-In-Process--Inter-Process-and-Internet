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

ifeq ($(BUILD),release)
# for release version
CPPFLAGS :=  -O3  -DNDEBUG -fPIC -I/usr/local/Cellar/boost/1.46.0/include
else
# for debug version
CPPFLAGS :=  -O0  -Wall -fPIC -g -pg -I/usr/local/Cellar/boost/1.46.0/include
endif
LDFLAGS := -L. -L$(BINDIR) -L$(LIBDIR) -Wl,-rpath,./ -Wl,-rpath-link,$(SHELLROOT)/$(BINDIR):$(SHELLROOT)/$(LIBDIR)
SHARED  := -shared
ARFLAGS := crs

all:

.PHONY: all clean

release:
	make "BUILD=release"

$(OBJDIR)/%.o:%.cpp
	$(CC) -c $(CPPFLAGS) $< -o $@

include $(SRCFILES:.cpp=.d)

%.d: %.cpp
	@set -e;rm -f $@; $(CC) -MM $(CPPFLAGS) $< > $@.$$$$; sed 's,\($*\)\.o[ :]*,$(OBJDIR)/\1.o $@ : ,g' < $@.$$$$ > $@; rm -f $@.$$$$


