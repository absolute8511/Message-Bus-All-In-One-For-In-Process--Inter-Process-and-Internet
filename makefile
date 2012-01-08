TARGET_DIRS := Core 
export ROOTPATH = $(shell pwd)
export BINDIR := $(ROOTPATH)/Bin
export OBJDIR := $(ROOTPATH)/Objs

NEEDED_PATH := $(TARGET_DIRS:%=$(ROOTPATH)/%) $(BINDIR) $(OBJDIR) 

all:
	@echo "you must choose to make for macosx or linux"

mac-all: $(NEEDED_PATH)
	$(foreach N,$(TARGET_DIRS),$(MAKE) -C $(N) -f makefile-mac;)

mac-releaseall: $(NEEDED_PATH)
	$(foreach N,$(TARGET_DIRS),$(MAKE) -C $(N) -f makefile-mac "BUILD=release";)

mac-cleanall: $(NEEDED_PATH)
	$(foreach N,$(TARGET_DIRS),$(MAKE) -C $(N) -f makefile-mac clean;)

linux-releaseall: $(NEEDED_PATH)
	$(foreach N,$(TARGET_DIRS),$(MAKE) -C $(N) "BUILD=release";)

linux-all: $(NEEDED_PATH)
	$(foreach N,$(TARGET_DIRS),$(MAKE) -C $(N);)

linux-cleanall: $(NEEDED_PATH)
	$(foreach N,$(TARGET_DIRS),$(MAKE) -C $(N) clean;)

webos-all: $(NEEDED_PATH)
	$(foreach N,$(TARGET_DIRS),$(MAKE) -C $(N) -f makefile-webos;)

webos-releaseall: $(NEEDED_PATH)
	$(foreach N,$(TARGET_DIRS),$(MAKE) -C $(N) -f makefile-webos "BUILD=release";)

webos-cleanall: $(NEEDED_PATH)
	$(foreach N,$(TARGET_DIRS),$(MAKE) -C $(N) -f makefile-webos clean;)


$(NEEDED_PATH):
	mkdir -p $(NEEDED_PATH)
