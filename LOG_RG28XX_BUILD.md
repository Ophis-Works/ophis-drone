# LOG_RG28XX_BUILD.md — branch rg28xx-build

## 2026-11-09 — Milestone 1 (build/deploy)
- Verified online (sources in PLAN_RG28XX_BUILD.md): RG28XX = H700
  aarch64, 640x480 panel; firmware on the owner's unit is an EmuELEC-style
  image ("EE clone" class). Existing R36S binary is compatible (glibc pin
  <= 2.28, same SDL2/ALSA DT_NEEDED); no source change needed for arch,
  panel or keymap until on-device testing says otherwise.
- Compiled the existing `make handheld` binary and packaged
  build/deploy/ophis-drone-rg28xx.zip with Roms/PORTS/ paths matching the
  device's layout (owner instruction: their roms folder is Roms/PORTS).
  Makefile automation for the RG28XX zip deferred until the layout is
  owner-confirmed.

## 2026-11-09 — Milestone 2 (ALSA S16_LE fallback)
- On-device test: app entered and immediately exited. ophis-drone.err:
  "ALSA lib pcm.c:8643:(snd_pcm_set_params) Sample format not available
  for PLAYBACK: Invalid argument / ALSA open failed". Cause: EmuELEC
  `default` ALSA device has no format plug; the H700 codec takes S16_LE,
  the app requested FLOAT_LE. (R36S `default` does the conversion.)
- TDD: added tests/test_f32tos16.c + Makefile rule FIRST (red: missing
  src/audio/f32tos16.{c,h}), then implemented the module — one warning
  fix on the way (missing <math.h> include for lrintf) — 10/10 green.
- Wired the fallback: alsa_pcm.c tries FLOAT, falls back to S16_LE +
  float→S16 conversion in the write path (scratch allocated at open, no
  steady-state malloc; close frees it). AUDIO_SRC gained f32tos16.c so
  desktop, tests and handheld all link it.
- Verification: make test = 11253/11253 passed (incl. new 10/10); fresh
  `make handheld` gate: GATE PASS, max GLIBC_2.17; desktop build clean.
- RG28XX zip rebuilt from the final binary.

## Open items
- On-device retest on the RG28XX (audio + full button matrix for the
  keymap check: A=RUN, B=TRIGGER, X/Y value +/-, L/R pages, Start=MENU,
  Select=MENU2, D-pad selection).
- Makefile `rg28xx` target once the deploy layout is confirmed.
