#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	achievements-viewer
APP_TITLE	:=	Achievements
APP_DESCRIPTION	:=	Starts the tracker and shows your achievements
APP_AUTHOR	:=	LiamTrader43
# The app's version: the release tag without its "v" (v1.4.0 -> 1.4.0). The app offers to
# update itself when GitHub's latest release is newer than this, so raise it for every release.
VERSION		:=	2.0.1
BUILD		:=	build
# Themes (source/theme.h): modern is in source/, every other theme is a folder themes/<id>/.
# The ones listed here are preinstalled: compiled into the app (each also needs its Theme in
# BUILT_IN in source/theme.c). The other folders are built as add-ons ("Theme add-ons" below).
BUILTIN_THEMES	:=	3ds
SOURCES		:=	source qrcodegen ../sysmodule/tracker/rcheevos/src ../sysmodule/tracker/rcheevos/src/rcheevos \
			$(foreach id,$(BUILTIN_THEMES),themes/$(id))
DATA		:=	data
INCLUDES	:=	include source qrcodegen ../sysmodule/tracker/source ../sysmodule/tracker/rcheevos/include ../sysmodule/tracker/rcheevos/src ../sysmodule/tracker/rcheevos/src/rcheevos
GRAPHICS	:=	gfx
GFXBUILD	:=	$(BUILD)
#ROMFS		:=	romfs
#GFXBUILD	:=	$(ROMFS)/gfx

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__ -DAPP_VERSION=\"$(VERSION)\"

# make BENCH=1: a drawing benchmark instead of the app (app/source/bench.h).
ifneq ($(BENCH),)
TARGET	:=	achievements-bench
BUILD	:=	build-bench
CFLAGS	+=	-DBENCH
endif

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(CTRULIB)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
#---------------------------------------------------------------------------------
export T3XFILES :=  $(GFXFILES:.t3s=.t3x)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean cia themes store

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES) themes $(if $(wildcard source/store_key.h),store)
	@# Which store key (if any) store_file.o was built with: written only when it changes.
	@s=$$(sha256sum source/store_key.h 2>/dev/null | cut -c1-64); s=$${s:-none}; \
	 [ "$$(cat $(BUILD)/store_key.state 2>/dev/null)" = "$$s" ] || echo "$$s" > $(BUILD)/store_key.state
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
# make cia: the app as a CIA with the logo's banner and icon (app/cia, from tools/cia_art.py).
# Its title version is VERSION (tools/cia_version.py): the 3DS won't install a CIA over the
# same title at the same version, so the app couldn't update itself as a CIA without it.
#---------------------------------------------------------------------------------
cia: all
	@bannertool makebanner -i cia/banner.png -a cia/banner.wav -o $(BUILD)/banner.bnr
	@bannertool makesmdh -s "3DS Achievements" -l "3DS Achievements" -p "LiamTrader43" -i cia/icon.png -o $(BUILD)/cia.smdh
	@makerom -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf cia/cia.rsf -icon $(BUILD)/cia.smdh \
		-banner $(BUILD)/banner.bnr -target t -exefslogo
	@python3 ../tools/cia_version.py $(TARGET).cia $(VERSION)
	@echo built ... $(TARGET).cia

#---------------------------------------------------------------------------------
# Theme add-ons (source/theme_api.h): each themes/<id>/ folder not in BUILTIN_THEMES is built on
# its own into themes/<id>.theme, for /achievements/themes on the SD card. Not part of the app.
# Linked by themes/theme.ld into the .theme format (our header first, the code on pages of its
# own), then objcopy keeps only those bytes; themes/<id>.elf stays, for looking up crash
# addresses. (A preinstalled theme can still be built as one, e.g. make themes/3ds.theme, to
# test the loader; the app skips an add-on with a preinstalled theme's id.)
#---------------------------------------------------------------------------------
THEME_IDS	:=	$(filter-out $(BUILTIN_THEMES),$(patsubst themes/%/,%,$(sort $(dir $(wildcard themes/*/*.c)))))
THEME_FILES	:=	$(foreach id,$(THEME_IDS),themes/$(id).theme)
THEME_CFLAGS	:=	-g -Wall -O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections $(ARCH) \
			-I$(CURDIR)/source $(INCLUDE) -D__3DS__ -DTHEME_MODULE -DAPP_VERSION=\"$(VERSION)\"
THEME_LDFLAGS	:=	-shared -nostdlib -Wl,--no-undefined -Wl,-z,defs -Wl,--gc-sections -T $(CURDIR)/themes/theme.ld
THEME_FORMAT	:=	$(shell sed -n 's/^.define THEME_FILE_FORMAT *\([0-9]*\).*/\1/p' source/theme_api.h)

themes: $(THEME_FILES)

.PRECIOUS: themes/%.elf
# (The file must end where its header says: a section left in by mistake would make it longer.
# Its format number, written by themes/theme.ld, must be the app's THEME_FILE_FORMAT.)
themes/%.theme: themes/%.elf
	@$(OBJCOPY) -O binary -R .dynamic -R .hash -R .dynsym -R .dynstr $< $@
	@[ $$(( $$(wc -c < $@) )) -eq $$(( $$(od -An -tu4 -j28 -N4 $@) )) ] || \
		{ echo "$@: its size isn't the one in its header (themes/theme.ld)"; rm -f $@; false; }
	@[ -n "$(THEME_FORMAT)" ] && [ $$(( $$(od -An -tu4 -j4 -N4 $@) )) -eq $(THEME_FORMAT) ] || \
		{ echo "$@: format in its header isn't THEME_FILE_FORMAT $(THEME_FORMAT) (themes/theme.ld)"; rm -f $@; false; }
	@echo built ... $@

.SECONDEXPANSION:
themes/%.elf: $$(wildcard themes/$$*/*.c) themes/module_rt.c themes/theme.ld $(wildcard source/*.h)
	@echo $(notdir $@)
	@$(CC) $(THEME_CFLAGS) $(THEME_LDFLAGS) -o $@ $(filter %.c,$^) -lgcc

#---------------------------------------------------------------------------------
# The store (source/store.c, store_file.h): what it sells is store/items.txt. tools/store_build.py
# encrypts it into items.bin, and each theme it lists (a themes/<id>/ add-on, built like the
# others; keep its source out of the public repository) into <id>.bin, with the key in
# source/store_key.h (tools/store_key.py makes it; never commit it). They go in the repository's
# achievements/store/ (STORE_OUT: commit them; the app downloads new and changed ones from there)
# and the SD card layout (STORE_DIST) for the release zip, each only written if it changed.
# "make" does it when the key is there, "make store" alone too. A build without the key can't
# open store files.
#---------------------------------------------------------------------------------
STORE_LIST	:=	store/items.txt
STORE_OUT	:=	../achievements/store
STORE_DIST	:=	../sysmodule/dist/achievements/store
STORE_THEMES	:=	$(shell awk '!/^[ \t]*(\#|$$)/ && $$3 == "theme" { print $$4 }' $(STORE_LIST) 2>/dev/null)

store: $(foreach id,$(STORE_THEMES),themes/$(id).theme) $(STORE_LIST) source/store_key.h
	@python3 ../tools/store_build.py source/store_key.h $(STORE_LIST) themes $(STORE_OUT) $(STORE_DIST)

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(GFXBUILD) $(wildcard themes/*.theme themes/*.elf)

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

# store_file.c compiles the store's key in when there is one (source/store_key.h): it's built
# again whenever that changes, even to or from no key (store_key.state, written by "all").
store_file.o : store_key.state

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x %.shbin
#---------------------------------------------------------------------------------
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

#---------------------------------------------------------------------------------
%.shbin.o %_shbin.h : %.shbin
#---------------------------------------------------------------------------------
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
