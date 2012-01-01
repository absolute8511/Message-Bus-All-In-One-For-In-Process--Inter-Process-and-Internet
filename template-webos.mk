# $< stand for the first file of prerequirefiles
# $@ stand for the target files
# $^ stand for all of the prerequirefiles
# %.o:%.cpp stand for the files which replace the %.o's suffix with .cpp
# use -gstabs+ or -gdwarf-2 to get more debug info ??
CC := /opt/PalmPDK/arm-gcc/bin/arm-none-linux-gnueabi-g++
AR := /opt/PalmPDK/arm-gcc/bin/arm-none-linux-gnueabi-ar
BINDIR := $(MAKEROOT)/Bin
LIBDIR := $(MAKEROOT)/Libs
OBJDIR := $(MAKEROOT)/Objs
SHELLROOT := $(shell pwd)

ifeq ($(BUILD),release)
# for release version
CPPFLAGS :=  -O3  -DNDEBUG -D_WEBOS_ -fPIC -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -I/usr/local/Cellar/boost/1.46.0/include -I/opt/PalmPDK/include \
	-I/opt/PalmPDK/include/SDL --sysroot=/opt/PalmPDK/arm-gcc/sysroot
else
# for debug version
CPPFLAGS :=  -O0 -D_WEBOS_  -Wall -fPIC -g -pg -I/usr/local/Cellar/boost/1.46.0/include -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -I/opt/PalmPDK/include -I/opt/PalmPDK/include/SDL --sysroot=/opt/PalmPDK/arm-gcc/sysroot
endif
LDFLAGS := -L. -L$(BINDIR) -L$(LIBDIR) 	-L/opt/PalmPDK/device/lib -Wl,--allow-shlib-undefined -lSDL -lGLESv2 -lpdl -Wl,-rpath,./ -Wl,-rpath-link,$(SHELLROOT)/$(BINDIR):$(SHELLROOT)/$(LIBDIR)
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


