CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

BUILD   := build
SRCS    := $(wildcard src/*.c)
CFLAGS  := -mcpu=cortex-m0 -mthumb -Os -g -std=gnu11 -Wall -Wextra \
           -ffunction-sections -fdata-sections -ffreestanding
LDFLAGS := -nostartfiles -Wl,--gc-sections -Wl,-Map=$(BUILD)/lhbs.map -T lpc11u37.ld

all: $(BUILD)/firmware.bin

$(BUILD)/lhbs.elf: $(SRCS) src/*.h lpc11u37.ld | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@ -lgcc
	$(SIZE) $@

# Named firmware.bin because that is the file the NXP USB ISP drive exposes.
$(BUILD)/firmware.bin: $(BUILD)/lhbs.elf
	$(OBJCOPY) -O binary $< $@
	python3 tools/lpcsum.py fix $@
	python3 tools/lpcsum.py check $@

$(BUILD):
	mkdir -p $@

# Host test of the multi-station round (four stations, each its own multi.c).
hosttest:
	mkdir -p $(BUILD)/host
	gcc -shared -fPIC -O1 -Wall -Isrc -o $(BUILD)/host/libstation.so src/multi.c src/params.c test/host_multi_station.c
	gcc -O1 -Wall -D_GNU_SOURCE -include unistd.h -o $(BUILD)/host/host_multi test/host_multi.c -ldl
	for seed in 1 2 3 4 5 6 7 8; do $(BUILD)/host/host_multi $(BUILD)/host/libstation.so $$seed || exit 1; done

clean:
	rm -rf $(BUILD)

.PHONY: all clean hosttest
