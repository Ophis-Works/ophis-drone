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

## 2026-11-09 — Milestone 3 (input self-mapping + rotation diagnostics)
- On-device test 2: app runs and stays up (the single ALSA line in the
  log is the FLOAT attempt warning — the S16 fallback succeeded), but the
  screen content is rotated 90 deg CW and no button works.
- Cause, input: EmuELEC SDL2 has no gamecontroller mapping for the H700
  pad (no "opened controller" line in the log) and the R36S PortMaster db
  paths do not exist there.
- Cause, screen: the RG28XX panel is natively portrait (DTS rotation=270);
  the firmware's SDL2/KMS presents our landscape content rotated. What
  exactly SDL presents (output size) must be measured before choosing the
  fix.
- main.c handheld block: joystick GUID self-mapping fallback (runtime
  SDL_JoystickGetGUIDString + built-in mainline-kernel H700 mapping:
  a:b1,b:b0,x:b3,y:b2, shoulders b4/b5, back/start/guide b8/b9/b10,
  D-pad as hat h0.x) + joystick probe line (name/guid/buttons/hats/axes)
  + display-mode and renderer-output-size diagnostics to stderr.
- Verification: make handheld gate PASS (GLIBC_2.17), tests 11253/11253;
  on-device run pending — the stderr lines decide the rotation fix and
  hat-vs-button D-pad.

## Open items
- On-device retest on the RG28XX (audio + full button matrix for the
  keymap check: A=RUN, B=TRIGGER, X/Y value +/-, L/R pages, Start=MENU,
  Select=MENU2, D-pad selection).
- Makefile `rg28xx` target once the deploy layout is confirmed.
