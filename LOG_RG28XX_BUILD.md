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

## 2026-11-09 — Milestone 3b (app-side rotation)
- On-device test 3: input works (self-mapping ok); diagnostics: display
  mode 480x640 driver=mali, renderer output 480x640 -> SDL sees the
  native portrait panel. SDL_ROTATION env in the launcher had no effect
  (this EmuELEC-family SDL2 has no rotation patch).
- Fix, app-side: render the 640x480 UI into an offscreen TARGET texture,
  then SDL_RenderCopyEx rotated onto the 480x640 output (exact 4:3 fit).
  OPHIS_ROT=90 env selects the other direction; default 270. Direct
  rendering fallback if the texture cannot be created. R36S path
  unchanged (scene == NULL there).
- ui_draw no longer calls SDL_RenderPresent (draw/present split — the
  present moves to main.c after the rotated blit); desktop behaviour
  identical (present after each redraw).
- Verification: make handheld gate PASS (GLIBC_2.17), tests 11253/11253,
  desktop build clean; direction confirmed on-device pending.

## 2026-11-09 — Milestone 3c (A/B swap + exact-fit blit)
- On-device test 4 (owner report): rotation direction correct; buttons
  work but printed A and B are swapped; MENU opens on the top M key
  (expected — the firmware maps it to BTN_START); Select = MENU2 to be
  confirmed. Video: content squeezed, two bars at the sides, top/bottom
  cropped.
- Input fix: measured raw indices differ from the mainline DTS pairing —
  mapping changed to a:b0,b:b1 (phys A=0, phys B=1); x:b3,y:b2 kept
  (values +/- untested by the owner — pending confirmation).
- Video fix: RenderCopyEx with NULL dst stretches the 640x480 texture
  into the 480x640 output BEFORE rotating (squeeze/bars/crop artifact).
  Present now uses the pre-rotation bounding box (-80,80,640,480) as
  dst; after the quarter turn the quad covers the output exactly.
- Verification: gate PASS, tests 11253/11253; on-device retest pending.

## 2026-11-09 — Milestone 4 (RG28XX as a separate compile target)
- Owner order: TARGET_R36S must stay byte-equal in behaviour to main —
  no shared handheld code path. TARGET_R36S now reverts to the exact
  main-state sequence (db load -> controller open -> logical size ->
  draw -> present -> close; the only shared change is SDL_RenderPresent
  moved from ui_draw.c into the main loop immediately after ui_draw —
  same call, same order). ALSA: R36S takes the FLOAT first-attempt path,
  unchanged.
- New compile target: -DTARGET_RG28XX, own object dir build/rg28xx/, own
  binary + gate + deploy zip (Roms/PORTS/ layout, owner-confirmed).
  HCFLAGS no longer bakes a target define; each obj rule passes its own.
- config.h: TARGET_RG28XX block (same KB_* logical-button values as the
  R36S block, RG28XX-specific comments: MENU on the top Menu key,
  portrait rendering note).
- main.c: pad self-mapping + scene/rotation + probe/diagnostic lines now
  compile under TARGET_RG28XX only; the explicit controller-open loop is
  shared by both handheld targets; desktop untouched.
- Tests: test_keymap_rg28xx + test_keymap_events_rg28xx added (same
  sources compiled with -DTARGET_RG28XX; guards updated to accept the
  new macro). Suite: 11323/11323 passed.
- Verification: clean rebuild of all three targets — desktop (0 errors),
  R36S gate PASS, RG28XX gate PASS (GLIBC_2.17), both zips packaged.

## Open items
- On-device retest on the RG28XX (audio + full button matrix for the
  keymap check: A=RUN, B=TRIGGER, X/Y value +/-, L/R pages, Start=MENU,
  Select=MENU2, D-pad selection).
- Makefile `rg28xx` target once the deploy layout is confirmed.
