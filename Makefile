ROM_DIR ?= ../arculator-mac/roms/riscos311
GEN = build/gen
HOSTOBJ = build/host
TRACES = --trace trace/boot

.PHONY: gen host dc cdi clean

gen:
	python3 tools/armxlat.py $(ROM_DIR) $(GEN) $(TRACES) --seeds trace/seeds.txt --ram-words trace/ramwords.txt

XLAT_SRC = $(wildcard $(GEN)/xlat_*.c)
XLAT_HOST = $(patsubst $(GEN)/%.c,$(HOSTOBJ)/%.o,$(XLAT_SRC))
HOSTCC = clang
HOSTFLAGS = -O1 -g -Isrc -DSTOP_ON_MISS -fno-strict-aliasing -Wno-unused-value -Wno-parentheses-equality

$(HOSTOBJ)/%.o: $(GEN)/%.c src/cpu.h
	@mkdir -p $(HOSTOBJ)
	@$(HOSTCC) $(HOSTFLAGS) -c $< -o $@

$(HOSTOBJ)/mach.o: src/mach.c src/cpu.h
	@mkdir -p $(HOSTOBJ)
	$(HOSTCC) $(HOSTFLAGS) -c $< -o $@

$(HOSTOBJ)/main.o: src/host/main.c src/cpu.h
	@mkdir -p $(HOSTOBJ)
	$(HOSTCC) $(HOSTFLAGS) -c $< -o $@

host: build/dreamrisc-host

build/dreamrisc-host: $(XLAT_HOST) $(HOSTOBJ)/mach.o $(HOSTOBJ)/main.o
	$(HOSTCC) -o $@ $^

# Dreamcast build; run through scripts/build-dc.sh, which sources the KOS environment.
DCOBJ = build/dc
XLAT_DC = $(patsubst $(GEN)/%.c,$(DCOBJ)/%.o,$(XLAT_SRC))
DCFLAGS = $(KOS_CFLAGS) -Os -fno-strict-aliasing -Wno-unused-value -Wno-unused-variable -Isrc

build/rom.bin: $(ROM_DIR)/IC24.ROM
	cat $(ROM_DIR)/IC24.ROM $(ROM_DIR)/IC25.ROM $(ROM_DIR)/IC26.rom $(ROM_DIR)/IC27.ROM > $@

$(DCOBJ)/%.o: $(GEN)/%.c src/cpu.h
	@mkdir -p $(DCOBJ)
	@$(KOS_CC) $(DCFLAGS) -c $< -o $@

$(DCOBJ)/mach.o: src/mach.c src/cpu.h
	@mkdir -p $(DCOBJ)
	$(KOS_CC) $(DCFLAGS) -c $< -o $@

$(DCOBJ)/main.o: src/dc/main.c src/cpu.h
	@mkdir -p $(DCOBJ)
	$(KOS_CC) $(DCFLAGS) $(if $(HW_LOG),-DHW_LOG) $(if $(HW_REPLAY),-DHW_REPLAY -Ibuild) -c $< -o $@

$(DCOBJ)/images.o: src/dc/images.S build/rom.bin assets/cmos.bin
	@mkdir -p $(DCOBJ)
	$(KOS_CC) -c $< -o $@

dc: build/dreamrisc.elf

build/dreamrisc.elf: $(XLAT_DC) $(DCOBJ)/mach.o $(DCOBJ)/main.o $(DCOBJ)/images.o
	kos-cc -o $@ $^ -Wl,-Map,build/dreamrisc.map

clean:
	rm -rf build
