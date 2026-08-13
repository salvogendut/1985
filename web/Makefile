# WASM POC build for the 1985 core.
#
# Compiles the PCW emulation core plus web stubs for host-only modules,
# with Emscripten. Run inside the my-distrobox container where emcc
# and the SDL3 headers live.
#
#   make clean && make -C web
#   python3 -m http.server 8080 --directory web/dist
#
# Produces a self-contained publish directory under dist/. Compiler
# intermediates stay under build/.

EMCC      = emcc
BUILD_DIR = build
DIST_DIR  = dist
CFLAGS    = -O2 -std=c11 -Wall -Wextra -I../src -Icompat -I.

SRCS = \
	../src/z80.c \
	../src/pcw.c \
	../src/mem.c \
	../src/asic.c \
	../src/fdc.c \
	../src/disk.c \
	../src/crtc.c \
	../src/kbd.c \
	../src/roller.c \
	../src/bootstrap.c \
	../src/beeper.c \
	../src/aysound.c \
	../src/paste.c

CORE_OBJS = $(patsubst ../src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
OBJS = $(CORE_OBJS) $(BUILD_DIR)/poc_main.o $(BUILD_DIR)/stubs.o
WASM_ASSETS = $(DIST_DIR)/1985.js $(DIST_DIR)/1985.wasm
BRAND_ICON = ../icons/512x512/apps/io.github.salvogendut.Emulator1985.png
STYLE_ASSETS = styles.css theme-pcw8256.css theme-retro-crt.css \
	theme-sapporo.css theme-sapporo-dark.css

all: $(DIST_DIR)/index.html $(addprefix $(DIST_DIR)/,$(STYLE_ASSETS)) $(DIST_DIR)/app.js \
	$(DIST_DIR)/gamepad.js $(DIST_DIR)/media-url.js \
	$(DIST_DIR)/brand-1985.png $(WASM_ASSETS)

test: test-gamepad test-media-url

test-gamepad:
	node test_gamepad.js

test-media-url:
	node test_media_url.js

$(WASM_ASSETS) &: $(OBJS) | $(DIST_DIR)
	$(EMCC) $(CFLAGS) $(OBJS) -o $(DIST_DIR)/1985.js \
	    -sMODULARIZE -sEXPORT_NAME=create1985 \
	    -sEXPORTED_FUNCTIONS='_poc_init,_poc_init_model,_poc_reset,_poc_step,_poc_pixels,_poc_key,_poc_joy,_poc_disk_motor,_poc_disk_activity,_poc_load_disk,_poc_load_disk_b,_poc_eject_disk,_poc_eject_disk_b,_poc_autorun,_poc_audio_reset,_poc_audio_avail,_poc_audio_read_pos,_poc_audio_buffer,_poc_audio_advance,_poc_width,_poc_height,_malloc,_free' \
	    -sEXPORTED_RUNTIME_METHODS='HEAPU8,HEAPU32,wasmMemory,getValue,setValue,ccall,cwrap,FS' \
	    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 \
	    --embed-file ../roms/pcw_boot.rom@roms/pcw_boot.rom

$(BUILD_DIR)/%.o: ../src/%.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/poc_main.o: poc_main.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c poc_main.c -o $@

$(BUILD_DIR)/stubs.o: stubs.c | $(BUILD_DIR)
	$(EMCC) $(CFLAGS) -c stubs.c -o $@

$(DIST_DIR)/index.html: index.html | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/gamepad.js: gamepad.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/media-url.js: media-url.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/%.css: %.css | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/app.js: app.js | $(DIST_DIR)
	cp $< $@

$(DIST_DIR)/brand-1985.png: $(BRAND_ICON) | $(DIST_DIR)
	cp $< $@

$(BUILD_DIR) $(DIST_DIR):
	mkdir -p $@

serve: all
	python3 -m http.server 8080 --directory $(DIST_DIR)

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
	rm -f *.o *.ppm poc.js poc.wasm 1985.js 1985.wasm

.PHONY: all clean serve test test-gamepad test-media-url
