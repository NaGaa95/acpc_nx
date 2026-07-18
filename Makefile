# Animal Crossing: Pocket Camp Complete Switch wrapper.
.SUFFIXES:
.DEFAULT_GOAL := all
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO in your environment. (export DEVKITPRO=/opt/devkitpro)")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET    := acpc_nx
APP_TITLE := Animal Crossing: Pocket Camp Complete
APP_AUTHOR := naga
APP_VERSION := 1.0.0
APP_ICON  := $(TOPDIR)/icon.jpg
export APP_TITLE APP_AUTHOR APP_VERSION APP_ICON
BUILD     := build
SOURCES   := source
INCLUDES  := source
ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -Wall -O2 -ffunction-sections -fdata-sections $(ARCH) $(DEFINES) \
           $(INCLUDE) -D__SWITCH__ -DNDEBUG
CFLAGS  += -DLOAD_ADDRESS=0xC0000000
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS := $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) \
           -Wl,--gc-sections,-Map,$(notdir $*.map)

LIBS := -lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lz -lnx -lm

LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := cacerts.pem

export LD := $(CXX)
export OFILES := $(SFILES:.s=.o) $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
                 $(addsuffix .o,$(BINFILES))
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(PORTLIBS)/include/SDL2 -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
DEPENDS := $(OFILES:.o=.d)
%.pem.o: %.pem
	$(bin2o)
main.o: cacerts.pem.o
NROFLAGS := --icon=$(APP_ICON) --nacp=$(OUTPUT).nacp
all : $(OUTPUT).nro
$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf : $(OFILES)
-include $(DEPENDS)
endif
