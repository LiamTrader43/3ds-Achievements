#---------------------------------------------------------------------------------
# Based on the CTRPluginFramework TestPlugin Makefile (libctrpf 0.8.0).
#
#   make          release build -> Achievements.3gx
#   make debug    debug build   -> Achievements-debug.3gx (links libctrpfd/libctrud)
#   make clean
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

export TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

CTRPFLIB	?=	$(DEVKITPRO)/libctrpf

TARGET		:=	Achievements
PSF			:=	$(TARGET).plgInfo
SOURCES		:=	src
INCLUDES	:=	src

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH		:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS		:=	-mword-relocations \
				-ffunction-sections -fdata-sections -fno-strict-aliasing \
				-Wall \
				$(ARCH) $(BUILD_CFLAGS) $(G)

# libctru 2.7 dropped NORETURN, but libctrpf 0.8.0 headers still use it.
DEFINES		:=	-D'NORETURN=__attribute__((noreturn))'

CFLAGS		+=	$(INCLUDE) -D__3DS__ $(DEFINES)

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS		:=	$(ARCH) $(G)
LDFLAGS		:=	-T $(TOPDIR)/3gx.ld $(ARCH) -Os -Wl,$(WL)--gc-sections,--section-start,.text=0x07000100

LIBS		:=	$(BUILD_LIBS) -lm
LIBDIRS		:=	$(CTRPFLIB) $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

CFILES			:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES			:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export LD		:=	$(CXX)
export OFILES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

# libctrpf headers include a bare "types.h", which is libctru's 3ds/types.h.
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CTRULIB)/include/3ds

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L $(dir)/lib)

# release/debug always recurse; the inner make handles incremental rebuilds.
.PHONY: all release debug clean re

#---------------------------------------------------------------------------------
all: release

release:
	@mkdir -p build/release
	@$(MAKE) BUILD=release OUTPUT=$(CURDIR)/$(TARGET).3gx BUILD_LIBS="-lctrpf -lctru" \
	WL=--strip-discarded,--strip-debug, BUILD_CFLAGS="-DNDEBUG=1 -O2 -fomit-frame-pointer" \
	DEPSDIR=$(CURDIR)/build/release --no-print-directory -C build/release -f $(CURDIR)/Makefile

debug:
	@mkdir -p build/debug
	@$(MAKE) BUILD=debug OUTPUT=$(CURDIR)/$(TARGET)-debug.3gx BUILD_LIBS="-lctrpfd -lctrud" \
	BUILD_CFLAGS="-DDEBUG=1 -Og" G=-g \
	DEPSDIR=$(CURDIR)/build/debug --no-print-directory -C build/debug -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr build *.elf *.3gx

re: clean all

#---------------------------------------------------------------------------------

else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT) : $(basename $(OUTPUT)).elf
$(basename $(OUTPUT)).elf : $(OFILES)

#---------------------------------------------------------------------------------
%.3gx: %.elf
	@echo creating $(notdir $@)
	@3gxtool -s $^ $(TOPDIR)/$(PSF) $@

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
