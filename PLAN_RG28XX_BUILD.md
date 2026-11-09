# PLAN_RG28XX_BUILD.md — RG28XX build support (branch rg28xx-build)

## DESIGN PLAN

### Requirement
Support the Anbernic RG28XX as a handheld build target, changing ONLY what
is necessary for the build/deploy to work.

### Verified facts (online sources, 2026-11-09)
- RG28XX hardware: Allwinner H700 (quad Cortex-A53, aarch64), 2.8" IPS
  640x480 panel, 1 GB RAM.
- The existing handheld build already targets aarch64 with a 640x480 panel
  (src/config.h TARGET_R36S block, src/ui/ui.h "640x480 logical"). No
  source change is needed for architecture or screen.
- RG28XX firmware options: ArkOS EE (RG35XX-plus family incl. RG28XX,
  Ubuntu userspace), Knulli, muOS, ROCKNIX. Any of them ships glibc
  >= 2.28, SDL2, ALSA.
- tools/handheld_gate.sh pins the binary to no required glibc version above
  GLIBC_2.28 -> the existing R36S binary is forward-compatible with every
  RG28XX firmware. DT_NEEDED (libSDL2-2.0.so.0, libasound.so.2, libm,
  libpthread, libc) all present.
- RG28XX ports deploy path is /roms/ports, same layout as the R36S zip
  (ports/Ophis-Drone.sh launcher + ports/ophis-drone/ program folder).
- Owner confirms the RG28XX has L/R shoulder buttons, so the R36S keymap
  (KB_TAB_BACK/KB_TAB_NEXT = 9/10) applies unchanged.
- tools/r36s-launcher.sh is generic (cd + exec + stderr redirect); it works
  unchanged on RG28XX ArkOS.

### UI/UX description
None. Desktop, R36S UI and keymap behavior are untouched. The RG28XX ships
the identical binary and identical 640x480 UI.

### System requirements check
- aarch64-linux-gnu-gcc 15.2.0 — present (BUILD_NOTES.md, measured).
- Multiarch libsdl2-dev:arm64, libasound2-dev:arm64 — present (existing
  `make handheld` builds and gates clean).
- zip, unzip, readelf, file — present.
Nothing missing.

## IMPLEMENTATION PLAN

### Method
Reuse the R36S compile output (one binary, one source of truth). Add a
Makefile packaging target only.

### Milestone 1 — Makefile target + documentation
- Makefile: new phony target `rg28xx` producing
  `build/deploy/ophis-drone-rg28xx.zip` from the same
  `build/r36s/ophis-drone` binary, `tools/r36s-launcher.sh` and
  `presets/*.json`. Reuses the existing staging recipes (no duplication).
- BUILD_NOTES.md: short section recording the RG28XX decision and the
  verified facts above (same aarch64/640x480, glibc <= 2.28 pin is
  compatible, /roms/ports layout, no-shoulder-button caveat).
- No source, keymap, test or launcher-template changes.

### Verification
1. `make handheld` — binary builds and GATE PASSes (unchanged gate).
2. `make rg28xx` — produces build/deploy/ophis-drone-rg28xx.zip.
3. `unzip -l build/deploy/ophis-drone-rg28xx.zip` shows
   ports/Ophis-Drone.sh, ports/ophis-drone/ophis-drone, presets/.
4. `make test` — all existing tests still pass (nothing touched).

### Milestone 2 — ALSA S16_LE fallback (on-device finding)

On EmuELEC the ALSA `default` maps straight to the H700 codec with no
format plug: `snd_pcm_set_params(..., SND_PCM_FORMAT_FLOAT_LE, ...)`
failed with "Sample format not available" and the app exited at startup.
The R36S firmware's `default` includes the plug, so the FLOAT path worked
there.

- Design: `alsa_open` keeps FLOAT as first choice and falls back to
  S16_LE on failure; a `use_s16` flag switches `alsa_write` to convert
  float→S16 into a scratch buffer allocated once at open (PERIOD frames
  x 2 ch; the engine writes exactly PERIOD). No steady-state malloc.
- New pure module `src/audio/f32tos16.c/.h` (stereo interleaved, clamp
  to [-1, 1], scale 32767, round-to-nearest) — unit-testable, SDL/ALSA
  free.
- TDD: tests/test_f32tos16.c written first (red: no module), then
  implementation, then green; full suite green.
- UI/UX: none; desktop path unchanged (FLOAT still first).

### Milestone 3 — commit
- Commit code + docs on branch rg28xx-build after M1 verification passes.
  Message: imperative, unsigned.
