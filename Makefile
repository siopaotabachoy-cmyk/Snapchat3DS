ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set. Build with GitHub Actions or devkitPro)
endif

include $(DEVKITARM)/3ds_rules

TARGET  := Snap3DS
OBJS    := main.o art.o
CTRU    := $(DEVKITPRO)/libctru
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -Wall -Wextra -O2 -mword-relocations -ffunction-sections \
           $(ARCH) -D__3DS__ -I$(CTRU)/include
LDFLAGS := -specs=3dsx.specs $(ARCH)
LIBS    := -L$(CTRU)/lib -lctru -lm

.PHONY: all clean
.DEFAULT_GOAL := all

all: $(TARGET).3dsx

%.o: %.c art.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(TARGET).smdh:
	smdhtool --create "Snap3DS" "Camera filters for New 3DS" "Snap3DS" $(CTRU)/default_icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $(TARGET).elf $@ --smdh=$(TARGET).smdh

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).smdh $(TARGET).3dsx
