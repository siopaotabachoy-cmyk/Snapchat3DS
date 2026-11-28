ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set. Build with GitHub Actions or devkitPro)
endif

include $(DEVKITARM)/3ds_rules

TARGET  := Snap3DS
OBJS    := main.o art.o extra.o memories.o chat.o snapmap.o brain.o stories.o spotlight.o
# code files may be in source/ or next to this Makefile
SRC     := $(if $(wildcard source/main.c),source,.)
vpath %.c $(SRC)
CTRU    := $(DEVKITPRO)/libctru
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -Wall -Wextra -O2 -mword-relocations -ffunction-sections \
           $(ARCH) -D__3DS__ -I$(CTRU)/include
LDFLAGS := -specs=3dsx.specs $(ARCH)
LIBS    := -L$(CTRU)/lib -lctru -lm

.PHONY: all clean
.DEFAULT_GOAL := all

all: $(TARGET).3dsx

%.o: %.c $(SRC)/art.h $(SRC)/extra.h $(SRC)/memories.h $(SRC)/chat.h $(SRC)/snapmap.h $(SRC)/brain.h $(SRC)/stories.h $(SRC)/spotlight.h
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(TARGET).smdh:
	smdhtool --create "Snap3DS" "Camera filters for New 3DS" "Snap3DS" $(CTRU)/default_icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $(TARGET).elf $@ --smdh=$(TARGET).smdh

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).smdh $(TARGET).3dsx
