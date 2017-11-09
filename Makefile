#
# Makefile for Wall-O-Matic Interface Board Firmware
#

# Output directors to store intermediate compiled files
# relative to the project directory
BUILD_BASE	= build
FW_BASE		= firmware

# Base directory for the compiler
XTENSA_TOOLS_ROOT ?= /opt/Espressif/crosstool-NG/builds/xtensa-lx106-elf/bin

# Base directory of the ESP8266 SDK package, absolute
SDK_BASE	?= /opt/Espressif/ESP8266_SDK

# Various paths from the SDK used in this project
SDK_LIBDIR	= lib
SDK_LDDIR	= ld
SDK_INCDIR	= include include/json driver_lib/include

# esptool.py path and port
ESPTOOL		?= esptool.py
ESPPORT		?= /dev/ttyUSB0
ESPBAUD		?= 460800
ESPTOOL_OPTS=--port $(ESPPORT) --baud $(ESPBAUD)

# Appgen path and name
APPGEN		?= $(SDK_BASE)/tools/gen_appbin.py

# SPI flash size, in K
ESP_SPI_FLASH_SIZE_K=4096
# 0: QIO, 1: QOUT, 2: DIO, 3: DOUT
ESP_FLASH_MODE=0
# 0: 40MHz, 1: 26MHz, 2: 20MHz, 15: 80MHz
ESP_FLASH_FREQ_DIV=0
# 2: 1024 KB (512 KB + 512 KB), 4: 4096 KB (512 KB + 512 KB)
ESP_FLASH_SIZE_MAP=4

ESP_BOOT_BIN = "$(SDK_BASE)/bin/boot_v1.7.bin"

# Name for the target project
TARGET		= app
TAGNAME		= "wallbox"
BUILD_DESCRIBE := $(shell git describe --long --match 'v[0-9]*.*' --dirty)

# Which modules (subdirectories) of the project to include in compiling
MODULES		= driver user
EXTRA_INCDIR= include libesphttpd/include

# Libraries used in this project, mainly provided by the SDK
LIBS		= c gcc hal phy pp net80211 wpa main lwip driver

# Additional libraries
LIBS		+= esphttpd webpages-espfs

# Compiler flags using during compilation of source files
CFLAGS		= -Os -g -O2 -Wpointer-arith -Wundef -Werror -Wall -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH

# Additional compiler flags
CFLAGS		+= -DBUILD_DESCRIBE=\"$(BUILD_DESCRIBE)\" -DOTA_TAGNAME=\"$(TAGNAME)\"

# Linker flags used to generate the main object file
LDFLAGS		= -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static

# Linker script used for the above linker step
LD_FILE_USR1 = eagle.app.v6.new.1024.app1.ld
LD_FILE_USR2 = eagle.app.v6.new.1024.app2.ld

LD_SCRIPT_USR1	:= $(addprefix -T$(SDK_BASE)/$(SDK_LDDIR)/,$(LD_FILE_USR1))
LD_SCRIPT_USR2	:= $(addprefix -T$(SDK_BASE)/$(SDK_LDDIR)/,$(LD_FILE_USR2))

# Select which tools to use as compiler, librarian and linker
CC		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
AR		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-ar
LD		:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
OBJCOPY	:= $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-objcopy

####
#### No user configurable options below here
####
SRC_DIR		:= $(MODULES)
BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(MODULES))

SDK_LIBDIR	:= $(addprefix $(SDK_BASE)/,$(SDK_LIBDIR))
SDK_INCDIR	:= $(addprefix -I$(SDK_BASE)/,$(SDK_INCDIR))

SRC			:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ			:= $(patsubst %.c,$(BUILD_BASE)/%.o,$(SRC))
LIBS		:= $(addprefix -l,$(LIBS))
APP_AR		:= $(addprefix $(BUILD_BASE)/,$(TARGET)_app.a)

INCDIR			:= $(addprefix -I,$(SRC_DIR))
EXTRA_INCDIR	:= $(addprefix -I,$(EXTRA_INCDIR))
MODULE_INCDIR	:= $(addsuffix /include,$(INCDIR))

TARGET_OUT_USR1	:= $(addprefix $(BUILD_BASE)/,$(TARGET).user1.out)
TARGET_OUT_USR2	:= $(addprefix $(BUILD_BASE)/,$(TARGET).user2.out)
TARGET_OUT		:=  $(TARGET_OUT_USR1) $(TARGET_OUT_USR2)

TARGET_BIN_USR1 := $(addprefix $(BUILD_BASE)/,$(TARGET).user1.bin)
TARGET_BIN_USR2 := $(addprefix $(BUILD_BASE)/,$(TARGET).user2.bin)
TARGET_BIN		:= $(TARGET_BIN_USR1) $(TARGET_BIN_USR2)
TARGET_OTAFILE	:= $(addprefix $(BUILD_BASE)/,$(TARGET).ota)

V ?= $(VERBOSE)
ifeq ("$(V)","1")
Q :=
vecho := @true
else
Q := @
vecho := @echo
endif

vpath %.c $(SRC_DIR)

define compile-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(INCDIR) $(MODULE_INCDIR) $(EXTRA_INCDIR) $(SDK_INCDIR) $(CFLAGS) -c $$< -o $$@
endef

define genappbin
$(1): $$(APP_AR) libesphttpd/libwebpages-espfs.a
	$$(vecho) LD $$@
	$$(Q) $$(LD) -Llibesphttpd -L$$(SDK_LIBDIR) $(2) $$(LDFLAGS) -Wl,--start-group $$(LIBS) $$(APP_AR) -Wl,--end-group -o $$@

$(3): $(1)
	$$(vecho) APPGEN $$@
	$$(Q) $$(OBJCOPY) --only-section .text -O binary $1 build/eagle.app.v6.text.bin
	$$(Q) $$(OBJCOPY) --only-section .data -O binary $1 build/eagle.app.v6.data.bin
	$$(Q) $$(OBJCOPY) --only-section .rodata -O binary $1 build/eagle.app.v6.rodata.bin
	$$(Q) $$(OBJCOPY) --only-section .irom0.text -O binary $1 build/eagle.app.v6.irom0text.bin
	$$(Q) cd build; COMPILE=gcc PATH=$$(XTENSA_TOOLS_ROOT):$$(PATH) python $$(APPGEN) $(1:build/%=%) 2 $$(ESP_FLASH_MODE) $$(ESP_FLASH_FREQ_DIV) $$(ESP_FLASH_SIZE_MAP) $(4)
	$$(Q) rm -f eagle.app.v6.*.bin
	$$(Q) mv build/eagle.app.flash.bin $$@
	@echo "** user$(4).bin uses $$$$(stat -c '%s' $$@) bytes of" $$(ESP_FLASH_MAX) "available"
endef

.PHONY: all checkdirs flash clean libesphttpd

all: checkdirs $(TARGET_OUT) $(FW_BASE)

$(eval $(call genappbin,$(TARGET_OUT_USR1),$$(LD_SCRIPT_USR1),$$(TARGET_BIN_USR1),1))
$(eval $(call genappbin,$(TARGET_OUT_USR2),$$(LD_SCRIPT_USR2),$$(TARGET_BIN_USR2),2))

libesphttpd/Makefile:
	$(Q) echo "No libesphttpd submodule found. Using git to fetch it..."
	$(Q) git submodule init
	$(Q) git submodule update

libesphttpd: libesphttpd/Makefile
	$(Q) make -C libesphttpd

libesphttpd/mkupgimg/mkupgimg: libesphttpd/mkupgimg/
	make -C libesphttpd/mkupgimg/

$(APP_AR): libesphttpd $(OBJ)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $(OBJ)

checkdirs: $(BUILD_DIR) $(FW_BASE)

$(BUILD_DIR):
	$(Q) mkdir -p $@

$(FW_BASE): $(TARGET_BIN) libesphttpd/mkupgimg/mkupgimg
	$(Q) libesphttpd/mkupgimg/mkupgimg \
		$(TARGET_BIN_USR1) $(TARGET_BIN_USR2) \
		$(TAGNAME) $(TARGET_OTAFILE)

flash: $(TARGET_OUT) $(FW_BASE)
	$(Q) $(ESPTOOL) $(ESPTOOL_OPTS) write_flash \
		0x00000 $(ESP_BOOT_BIN) \
		0x01000 $(TARGET_BIN_USR1) \
		0x80000 $(TARGET_BIN_USR2)

blankflash:
	$(Q) $(ESPTOOL) $(ESPTOOL_OPTS) write_flash \
		0x00000 $(ESP_BOOT_BIN) \
    	0x3fc000 $(SDK_BASE)/bin/esp_init_data_default.bin \
    	0x7e000 $(SDK_BASE)/bin/blank.bin \
    	0x3fe000 $(SDK_BASE)/bin/blank.bin

clean:
	$(Q) make -C libesphttpd clean
	$(Q) rm -rf $(FW_BASE) $(BUILD_BASE)

$(foreach bdir,$(BUILD_DIR),$(eval $(call compile-objects,$(bdir))))
