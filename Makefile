# ophis-drone build (IMPLEMENTATION.md §5)
WARNFLAGS = -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wfloat-equal -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wshadow -Wundef -Wpointer-arith -Wwrite-strings -Wredundant-decls -Wold-style-definition -Wmissing-field-initializers -Wswitch-enum -Wformat=2
CFLAGS  = -std=c23 -O2 $(WARNFLAGS) -D_POSIX_C_SOURCE=200809L \
          -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
          -isystem third_party
DEPS    = $(shell pkg-config --cflags --libs sdl2 alsa)
CC      = gcc

SRC_APP = src/app/main.c src/app/autoquit.c
DSP_SRC = src/dsp/osc.c src/dsp/prng.c src/dsp/subosc.c src/dsp/adsr.c \
          src/dsp/filter.c src/dsp/chain.c src/dsp/mixer.c src/dsp/fx.c
SEQ_SRC = $(wildcard src/seq/*.c)
PRESET_SRC = $(wildcard src/preset/*.c)
AUDIO_SRC = src/audio/alsa_pcm.c src/audio/mailbox.c src/audio/engine.c \
            src/audio/f32tos16.c
# font8x8.c: a real TU (AMENDED, owner order) — the embedded 8x8 face
# plus the 10x10 per-pixel scaled cache (font_glyph10), PLAN §8.6
# seq_items.c: the pure SEQ page item table (AMENDED, owner order,
# PLAN §8.2) — plain data linked into every UI build, SDL-free
UI_SRC    = src/ui/keymap.c src/ui/ui.c src/ui/ui_menu.c src/ui/ui_draw.c \
            src/ui/valrepeat.c \
            src/ui/theme.c src/ui/range.c src/ui/seq_items.c src/ui/font8x8.c
TESTS   = test_sanity test_osc test_adsr test_filter test_chain \
          test_engine_seq test_mailbox \
          test_mixer test_sequencer test_transport test_json test_preset \
          test_fx test_presdirs test_engine test_keymap test_keymap_r36s \
          test_keymap_events test_keymap_events_r36s \
          test_theme \
          test_range test_seq_items test_font test_ui_menu \
          test_autoquit test_f32tos16 \
          test_keymap_rg28xx test_keymap_events_rg28xx

.PHONY: all handheld rg28xx test clean
.DELETE_ON_ERROR:

all: build/desktop/ophis-drone

build/desktop/ophis-drone: $(SRC_APP) src/config.h $(AUDIO_SRC) $(UI_SRC) \
                           $(DSP_SRC) $(SEQ_SRC) $(PRESET_SRC) \
                           | build/desktop
	$(CC) $(CFLAGS) -Isrc -DTARGET_DESKTOP $(SRC_APP) $(AUDIO_SRC) \
	    $(UI_SRC) $(DSP_SRC) $(SEQ_SRC) $(PRESET_SRC) $(DEPS) \
	    -Wl,-z,relro,-z,now -lpthread -lm -o $@

build/desktop build/r36s build/tests build/r36s/obj/compat \
build/rg28xx build/rg28xx/obj/compat build/deploy/ports \
build/deploy/Roms/PORTS:
	mkdir -p $@

# --- handheld (R36S, PLAN.md §11 M6, owner ruling) --------------------------
# Build ON THE HOST with aarch64-linux-gnu-gcc; glibc-2.28 target met with
# symbol pinning, not static linking / container:
#   - fully dynamic link against arm64 SDL2/ALSA (/usr/lib/aarch64-linux-gnu);
#   - pow/log2/pthread_create/pthread_join wrapped to GLIBC_2.17 aliases
#     (src/compat/glibc_symver.c, -Wl,--wrap);
#   - custom crt0.s pins __libc_start_main@GLIBC_2.17 (-nostartfiles);
#   - GATE (tools/handheld_gate.sh): readelf -V must show no required glibc
#     version above GLIBC_2.28 and file must say aarch64 ELF.
# Outputs: build/r36s/ophis-drone, build/deploy/ports/ layout and
# build/deploy/ophis-drone-r36s.zip (build/ is gitignored).
CROSS       = aarch64-linux-gnu-gcc
HCFLAGS     = -std=c23 -O2 $(WARNFLAGS) -D_POSIX_C_SOURCE=200809L \
              -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
              -Isrc -isystem third_party -isystem /usr/include/SDL2
HANDHELD_SRC = $(SRC_APP) $(AUDIO_SRC) $(UI_SRC) $(DSP_SRC) $(SEQ_SRC) \
               $(PRESET_SRC) src/compat/glibc_symver.c
HANDHELD_OBJ = $(patsubst src/%.c,build/r36s/obj/%.o,$(HANDHELD_SRC)) \
               build/r36s/obj/compat/crt0.o
RG28XX_OBJ   = $(patsubst src/%.c,build/rg28xx/obj/%.o,$(HANDHELD_SRC)) \
               build/rg28xx/obj/compat/crt0.o
ARM64_LIB   = /usr/lib/aarch64-linux-gnu
DEPLOY      = build/deploy

handheld: build/r36s/ophis-drone $(DEPLOY)/ophis-drone-r36s.zip

build/r36s/ophis-drone: $(HANDHELD_OBJ) | build/r36s/obj/compat
	$(CROSS) -nostartfiles -no-pie $(HANDHELD_OBJ) -L$(ARM64_LIB) \
	    -Wl,--wrap=pow,--wrap=log2,--wrap=exp,--wrap=pthread_create,--wrap=pthread_join \
	    -Wl,--no-as-needed \
	    -lSDL2 -lasound -lm -lpthread -ldl \
	    $(ARM64_LIB)/libpthread.so.0 -o $@
	tools/handheld_gate.sh $@

build/r36s/obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(CROSS) $(HCFLAGS) -DTARGET_R36S -c $< -o $@

build/r36s/obj/compat/crt0.o: src/compat/crt0.s | build/r36s/obj/compat
	$(CROSS) -c $< -o $@

# --- RG28XX (Allwinner H700, EmuELEC-family firmware) -----------------------
# Own compile target: -DTARGET_RG28XX (portrait-panel rendering, H700 pad
# self-mapping, separate binary and zip). Same glibc pinning as the R36S.
rg28xx: build/rg28xx/ophis-drone $(DEPLOY)/ophis-drone-rg28xx.zip

build/rg28xx/ophis-drone: $(RG28XX_OBJ) | build/rg28xx/obj/compat
	$(CROSS) -nostartfiles -no-pie $(RG28XX_OBJ) -L$(ARM64_LIB) \
	    -Wl,--wrap=pow,--wrap=log2,--wrap=exp,--wrap=pthread_create,--wrap=pthread_join \
	    -Wl,--no-as-needed \
	    -lSDL2 -lasound -lm -lpthread -ldl \
	    $(ARM64_LIB)/libpthread.so.0 -o $@
	tools/handheld_gate.sh $@

build/rg28xx/obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(CROSS) $(HCFLAGS) -DTARGET_RG28XX -c $< -o $@

build/rg28xx/obj/compat/crt0.o: src/compat/crt0.s | build/rg28xx/obj/compat
	$(CROSS) -c $< -o $@

# Deploy staging: program folder ports/ophis-drone/ with the binary, and the
# executable launcher ports/Ophis-Drone.sh OUTSIDE the folder (EmulationStation
# lists it; it cds to its own directory and execs the binary).
$(DEPLOY)/ports/ophis-drone/ophis-drone: build/r36s/ophis-drone | $(DEPLOY)/ports
	@mkdir -p $(@D)
	cp $< $@

$(DEPLOY)/ports/Ophis-Drone.sh: tools/r36s-launcher.sh | $(DEPLOY)/ports
	cp $< $@
	chmod +x $@

# Preset staging: presets/*.json ship inside the program folder (the
# launcher cds there, PLAN_KEYMAP_REBIND §6). The staged directory is
# rebuilt from the current presets/ by the zip recipe itself (F13): a
# per-file pattern rule leaves stale copies behind when a preset is
# renamed or deleted (the wildcard drops it from the prerequisites and
# only 'make clean' removes staging). rm -rf + fresh copy on every zip
# build means stale files cannot survive.
PRESETS      = $(wildcard presets/*.json)

$(DEPLOY)/ophis-drone-r36s.zip: $(DEPLOY)/ports/ophis-drone/ophis-drone \
                                $(DEPLOY)/ports/Ophis-Drone.sh \
                                $(PRESETS)
	rm -rf $(DEPLOY)/ports/ophis-drone/presets
	mkdir -p $(DEPLOY)/ports/ophis-drone/presets
	# no bundled controller db: the app uses the OS databases only — a
	# stale staged copy from an older build must not ship (F13 lesson)
	rm -f $(DEPLOY)/ports/ophis-drone/gamecontrollerdb.txt
	for f in presets/*.json; do \
	  if [ -e "$$f" ]; then cp "$$f" $(DEPLOY)/ports/ophis-drone/presets/; fi; \
	done
	rm -f $@
	cd $(DEPLOY) && zip -r ophis-drone-r36s.zip ports
	@echo "HANDHELD OK: $@"

# RG28XX staging: same program-folder layout, zipped with the device's
# Roms/PORTS/ paths (owner-confirmed layout) so the zip extracts at the
# SD card's roms-partition root. The launcher template is generic.
$(DEPLOY)/Roms/PORTS/ophis-drone/ophis-drone: build/rg28xx/ophis-drone \
                                             | $(DEPLOY)/Roms/PORTS
	@mkdir -p $(@D)
	cp $< $@

$(DEPLOY)/Roms/PORTS/Ophis-Drone.sh: tools/r36s-launcher.sh | $(DEPLOY)/Roms/PORTS
	cp $< $@
	chmod +x $@

$(DEPLOY)/ophis-drone-rg28xx.zip: $(DEPLOY)/Roms/PORTS/ophis-drone/ophis-drone \
                                  $(DEPLOY)/Roms/PORTS/Ophis-Drone.sh \
                                  $(PRESETS)
	rm -rf $(DEPLOY)/Roms/PORTS/ophis-drone/presets
	mkdir -p $(DEPLOY)/Roms/PORTS/ophis-drone/presets
	rm -f $(DEPLOY)/Roms/PORTS/ophis-drone/gamecontrollerdb.txt
	for f in presets/*.json; do \
	  if [ -e "$$f" ]; then cp "$$f" $(DEPLOY)/Roms/PORTS/ophis-drone/presets/; fi; \
	done
	rm -f $@
	cd $(DEPLOY) && zip -r ophis-drone-rg28xx.zip Roms
	@echo "RG28XX OK: $@"

# Unit tests: src/dsp/* only — no SDL/ALSA (IMPLEMENTATION.md §5).
test: $(TESTS:%=build/tests/%)
	@pass=0; total=0; failed=0; \
	for b in $(TESTS); do \
	  printf '%-14s ' "$$b"; \
	  line=`build/tests/$$b`; rc=$$?; \
	  echo "$$line"; \
	  if [ $$rc -ne 0 ]; then failed=1; fi; \
	  n=`printf '%s\n' "$$line" | tail -1 | sed 's|/.*||'`; \
	  t=`printf '%s\n' "$$line" | tail -1 | sed 's|.*/||; s| .*||'`; \
	  pass=`expr $$pass + $$n`; total=`expr $$total + $$t`; \
	done; \
	echo "TOTAL: $$pass/$$total passed"; \
	exit $$failed

# Per-test shortcut: 'make test_preset' builds and runs one test binary.
.PHONY: $(TESTS)
$(TESTS): test_%: build/tests/test_%
	@./build/tests/test_$*

# Unit tests: src/dsp/* + src/seq/* + src/preset/* only — no SDL/ALSA
# (IMPLEMENTATION.md §5). json.c has no deps; preset.c uses C stdio/mkdir.
build/tests/test_%: tests/test_%.c tests/harness.h $(DSP_SRC) $(SEQ_SRC) \
                    $(PRESET_SRC) | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_$*.c $(DSP_SRC) $(SEQ_SRC) \
	    $(PRESET_SRC) -o $@ -lm

# test_engine_seq: sequencer application at the engine level (PLAN §6
# amendment: step pitch persists; vel scales the level) — engine + DSP,
# NO ALSA (unlike test_engine).
build/tests/test_engine_seq: tests/test_engine_seq.c tests/harness.h \
                             $(DSP_SRC) $(SEQ_SRC) $(PRESET_SRC) \
                             src/audio/engine.c | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_engine_seq.c $(DSP_SRC) $(SEQ_SRC) \
	    $(PRESET_SRC) src/audio/engine.c -o $@ -lm

# test_autoquit: OPHIS_AUTOQUIT_MS parse helper (PLAN_CODE_REVIEW_FIXES.md
# Phase 6 P3) — SDL-free; links only src/app/autoquit.c.
build/tests/test_autoquit: tests/test_autoquit.c tests/harness.h \
                           src/app/autoquit.h src/app/autoquit.c | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_autoquit.c src/app/autoquit.c -o $@

# test_mailbox: lock-free channel contracts (PLAN_CODE_REVIEW_FIXES.md
# Phase 2, H1/H2) — SDL/ALSA-free; links only mailbox.c (+ pthread for the
# interleave validity tests).
build/tests/test_mailbox: tests/test_mailbox.c tests/harness.h \
                          src/audio/mailbox.h src/audio/mailbox.c | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_mailbox.c src/audio/mailbox.c \
	    -lpthread -o $@

# test_keymap: compile-level binding check + keymap_label() caller-buffer
# contract (F5) — config.h desktop KB_* must be OUR ophis_key_t ids.
# SDL-linked: labels come from SDL_GetKeyName via ui/keymap.c.
build/tests/test_keymap: tests/test_keymap.c tests/harness.h src/config.h \
                         src/ui/keymap.h src/ui/keymap.c | build/tests
	$(CC) $(CFLAGS) -Isrc -DTARGET_DESKTOP $(shell pkg-config --cflags sdl2) \
	    tests/test_keymap.c src/ui/keymap.c -o $@ \
	    $(shell pkg-config --libs sdl2)

# test_keymap_r36s: same check for the handheld target — config.h R36S
# KB_* must be SDL logical gamepad button constants (PLAN_KEYMAP_REBIND
# §1) + the keymap_label() short-form pins (PLAN_HINT_SHORT §1.3).
# SDL-linked like test_keymap_events_r36s: labels come from ui/keymap.c.
build/tests/test_keymap_r36s: tests/test_keymap_r36s.c tests/harness.h \
                              src/config.h src/ui/keymap.h src/ui/keymap.c \
                              | build/tests
	$(CC) $(CFLAGS) -Isrc -DTARGET_R36S $(shell pkg-config --cflags sdl2) \
	    tests/test_keymap_r36s.c src/ui/keymap.c -o $@ \
	    $(shell pkg-config --libs sdl2)

# test_keymap_events: event-level keymap_action() checks (remediation F1
# auto-repeat gate, F6 controller-button collision) — SDL-linked, builds
# SDL_Event structs. One source, two objects/targets mirroring
# test_keymap / test_keymap_r36s.
build/tests/test_keymap_events: tests/test_keymap_events.c tests/harness.h \
                                src/config.h src/ui/keymap.h \
                                src/ui/keymap.c | build/tests
	$(CC) $(CFLAGS) -Isrc -DTARGET_DESKTOP $(shell pkg-config --cflags sdl2) \
	    tests/test_keymap_events.c src/ui/keymap.c -o $@ \
	    $(shell pkg-config --libs sdl2)

build/tests/test_keymap_events_r36s: tests/test_keymap_events.c \
                                     tests/harness.h src/config.h \
                                     src/ui/keymap.h src/ui/keymap.c \
                                     | build/tests
	$(CC) $(CFLAGS) -Isrc -DTARGET_R36S $(shell pkg-config --cflags sdl2) \
	    tests/test_keymap_events.c src/ui/keymap.c -o $@ \
	    $(shell pkg-config --libs sdl2)

# test_keymap_rg28xx / test_keymap_events_rg28xx: the same compile-level
# binding checks for the RG28XX target (PLAN_RG28XX_BUILD M3) — same
# sources, compiled with -DTARGET_RG28XX against config.h's RG28XX block.
build/tests/test_keymap_rg28xx: tests/test_keymap_r36s.c tests/harness.h \
                                src/config.h src/ui/keymap.h src/ui/keymap.c \
                                | build/tests
	$(CC) $(CFLAGS) -Isrc -DTARGET_RG28XX $(shell pkg-config --cflags sdl2) \
	    tests/test_keymap_r36s.c src/ui/keymap.c -o $@ \
	    $(shell pkg-config --libs sdl2)

build/tests/test_keymap_events_rg28xx: tests/test_keymap_events.c \
                                       tests/harness.h src/config.h \
                                       src/ui/keymap.h src/ui/keymap.c \
                                       | build/tests
	$(CC) $(CFLAGS) -Isrc -DTARGET_RG28XX $(shell pkg-config --cflags sdl2) \
	    tests/test_keymap_events.c src/ui/keymap.c -o $@ \
	    $(shell pkg-config --libs sdl2)

# test_f32tos16: the float→S16 conversion helper (PLAN_RG28XX_BUILD M2)
# for the ALSA S16_LE fallback — SDL/ALSA-free pure-math module.
build/tests/test_f32tos16: tests/test_f32tos16.c tests/harness.h \
                           src/audio/f32tos16.h src/audio/f32tos16.c | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_f32tos16.c src/audio/f32tos16.c -o $@ -lm

# test_theme: WCAG AA contrast on the exported palette constants — SDL-free;
# definitions live in ui/theme.c (not yet implemented: red state).
build/tests/test_theme: tests/test_theme.c tests/harness.h src/ui/theme.h \
                        src/ui/theme.c | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_theme.c src/ui/theme.c -o $@ -lm

# test_range: hostile-input safety + ladder sanity for the pure position
# helpers (PLAN §8.3 ladders, log1p-based) — SDL-free pure-math module.
build/tests/test_range: tests/test_range.c tests/harness.h src/ui/range.h \
                        src/ui/range.c | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_range.c src/ui/range.c -o $@ -lm

# test_seq_items: the SEQ page item table (PLAN §8.2 AMENDED, owner
# order) — pure data, SDL-free; 7 items (NO BPM — the global tempo lives
# on MASTER), exact order, MODE/ONCE present, MASTER_ITEMS exported.
build/tests/test_seq_items: tests/test_seq_items.c tests/harness.h \
                            src/ui/seq_items.h src/ui/seq_items.c | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_seq_items.c src/ui/seq_items.c -o $@

# test_font: 10x10 scaled-font metrics + scaler invariants (PLAN §8.6
# AMENDED, owner order) — SDL-free; the scaled glyph cache lives in
# ui/font8x8.c.
build/tests/test_font: tests/test_font.c tests/harness.h src/ui/font8x8.h \
                       src/ui/font8x8.c | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_font.c src/ui/font8x8.c -o $@

# test_ui_menu: menu_action() contracts (PLAN_CODE_REVIEW_FIXES.md Phase 4,
# U4) — NAV/VAL inert in MENU_MAIN/CONFIRM/LOAD, MENU_NAME grid picker
# kept. SDL-linked (ui.c is an SDL TU); runs headless on the dummy video
# driver, no window. Preset I/O via the presets/ dir staged beside the test
# binary (build/tests/presets).
build/tests/test_ui_menu: tests/test_ui_menu.c tests/harness.h src/config.h \
                          src/ui/ui.h src/ui/ui_internal.h \
                          src/ui/ui.c src/ui/ui_menu.c src/ui/ui_draw.c \
                          src/ui/valrepeat.c src/ui/keymap.c src/ui/theme.c \
                          src/ui/range.c src/ui/seq_items.c \
                          src/ui/font8x8.c \
                          src/audio/mailbox.h src/audio/mailbox.c \
                          src/preset/defaults.h src/preset/defaults.c \
                          src/preset/preset.h src/preset/preset.c \
                          src/preset/schema.h src/preset/schema_parse.c src/preset/schema_tab.c \
                          src/preset/schema_write.c | build/tests
	$(CC) $(CFLAGS) -Isrc -DTARGET_DESKTOP $(shell pkg-config --cflags sdl2) \
	    tests/test_ui_menu.c src/ui/ui.c src/ui/ui_menu.c src/ui/ui_draw.c \
	    src/ui/valrepeat.c src/ui/keymap.c src/ui/theme.c \
	    src/ui/range.c src/ui/seq_items.c src/ui/font8x8.c \
	    src/audio/mailbox.c src/preset/defaults.c src/preset/preset.c \
	    src/preset/schema_parse.c src/preset/schema_tab.c \
                          src/preset/schema_write.c -o $@ $(shell pkg-config --libs sdl2) \
	    -lpthread -lm

# test_engine is the ONLY test linking ALSA (device "null", IMPLEMENTATION
# .md §5/§8 M4).
build/tests/test_engine: tests/test_engine.c tests/harness.h $(DSP_SRC) \
                         $(SEQ_SRC) $(PRESET_SRC) $(AUDIO_SRC) | build/tests
	$(CC) $(CFLAGS) -Isrc tests/test_engine.c $(DSP_SRC) $(SEQ_SRC) \
	    $(PRESET_SRC) $(AUDIO_SRC) $(shell pkg-config --libs alsa) \
	    -lpthread -o $@ -lm

clean:
	rm -rf build
