.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:=	fc3ds
BUILD		:=	build
SOURCES		:=	source source/app source/data source/ui source/scenes \
			source/net \
			source/qr vendor/quirc \
			vendor/cjson vendor/ctr-osk-rt/source
DATA		:=	data
INCLUDES	:=	source vendor vendor/ctr-osk-rt/include
GRAPHICS	:=	gfx
ROMFS		:=	romfs
GFXBUILD	:=	$(ROMFS)/gfx

APP_TITLE	:=	Font Changer
APP_DESCRIPTION	:=	Install 3DS system fonts from the internet
APP_AUTHOR	:=	ada

ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -Wextra -Wno-unused-parameter -O2 -mword-relocations \
			-ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# Link order matters: curl before mbedtls, mbedtls before x509 before crypto,
# freetype before the compression libs it pulls in.
LIBS	:= -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
	   -lfreetype -lpng16 -lbz2 -lz -lctru -lm

LIBDIRS	:= $(CTRULIB) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

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

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

ifeq ($(GFXBUILD),$(BUILD))
export T3XFILES :=  $(GFXFILES:.t3s=.t3x)
else
export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
endif

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

# freetype2 keeps its headers in a subdirectory the generic sweep above misses.
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(PORTLIBS)/include/freetype2 \
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

APP_PRODUCT_CODE := CTR-P-FC3D
APP_UNIQUE_ID    := 0xFCF23
BANNER_IMAGE     := banner.png
ICON_IMAGE       := icon.png

.PHONY: all clean test run cia

all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES)
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

test:
	@$(MAKE) --no-print-directory -C tests test

run: all
	@3dslink $(if $(THREEDS_IP),-a $(THREEDS_IP),) $(TARGET).3dsx

cia: all
	@echo "building banner and icon ..."
	@bannertool makebanner -i $(BANNER_IMAGE) -a $(TOPDIR)/audio/silent.wav \
		-o $(BUILD)/banner.bnr 2>/dev/null || \
	 bannertool makebanner -i $(BANNER_IMAGE) -o $(BUILD)/banner.bnr
	@bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" \
		-p "$(APP_AUTHOR)" -i $(ICON_IMAGE) -o $(BUILD)/icon.icn
	@echo "building $(TARGET).cia ..."
	@makerom -f cia -o $(TARGET).cia -elf $(OUTPUT).elf -rsf $(TARGET).rsf \
		-icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr \
		-exefslogo -target t -DAPP_ROMFS=$(TOPDIR)/$(ROMFS)
	@echo "built ... $(TARGET).cia"

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).cia $(OUTPUT).smdh $(TARGET).elf $(GFXBUILD)
	@$(MAKE) --no-print-directory -C tests clean 2>/dev/null || true

$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

else

# Vendored code builds with our warnings relaxed; upstream will not fix them.
cJSON.o quirc.o decode.o identify.o version_db.o: CFLAGS += \
	-Wno-sign-compare -Wno-type-limits -Wno-unused-but-set-variable

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

.PRECIOUS	:	%.t3x %.shbin
%.t3x.o	%_t3x.h :	%.t3x
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

%.shbin.o %_shbin.h : %.shbin
	$(SILENTMSG) $(notdir $<)
	$(bin2o)

-include $(DEPSDIR)/*.d

endif
