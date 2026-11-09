/* src/app/main.c — M5: real UI loop (IMPLEMENTATION.md §6, PLAN §4).
 * UI thread (SDL events + draw) and host thread (pthreads) running the
 * engine via ALSA over the lock-free mailbox. Event-driven redraw with a
 * 100 ms status refresh (<= 10 Hz, PLAN §8.6). No quit key — exit is
 * menu-only (PLAN §2). On exit the LAST line printed is: XRUNS <n>
 * (desktop gate: must be 0). Set OPHIS_UI_LOG=1 to log each mailbox
 * commit with a one-line param delta summary (M5 smoke aid). */
#include <SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "../config.h"
#include "autoquit.h"
#include "../audio/alsa_pcm.h"
#include "../audio/engine.h"
#include "../audio/mailbox.h"
#include "../preset/defaults.h"
#include "../ui/ui.h"

#define SR         44100
#define PERIOD     256
#define REFRESH_MS 100                  /* status refresh <= 10 Hz */

static mailbox_t   mb;
static alsa_pcm_t *g_pcm = NULL;
static _Atomic int quit  = 0;
static _Atomic int audio_ready = 0;
static int         g_log = 0;
#if defined(TARGET_R36S)
static SDL_GameController *g_ctrl = NULL;
#endif

/* --- host thread: engine + ALSA (PLAN §4 architecture) ----------------- */
static void *audio_thread_fn(void *arg)
{
    (void)arg;
    static engine_t eng;                    /* big object, not on the stack */
    engine_init(&eng, (double)SR);

    g_pcm = alsa_open("default", SR, PERIOD);
    if (!g_pcm) {
        fprintf(stderr, "ALSA open failed\n");
        atomic_store(&quit, 1);
        return NULL;
    }
    atomic_store(&audio_ready, 1);

    float block[PERIOD * 2];
    synth_params_t snap;
    ui_status_t st;

    while (!atomic_load_explicit(&quit, memory_order_relaxed)) {
        mailbox_read(&mb, &snap);                       /* e.p per block */
        eng.p = snap;
        int trig = atomic_exchange(&mb.trigger_req, 0); /* fetch-exchange */
        int pan  = atomic_exchange(&mb.panic_req, 0);
        int rmask = atomic_exchange(&mb.retrig_req, 0);
        engine_render_block(&eng, block, PERIOD, trig, pan, rmask);

        st.run  = eng.p.run;
        st.gate = eng.tr.gate;
        for (int c = 0; c < 3; c++)
            st.env[c] = eng.ch[c].env.level;
        /* B-SEQ1 (owner order 2026-11-05): the live step is the
         * engine's step WINDOW — mode-independent; never the
         * sequencer's order cursor (frozen at 0 in random/markov) */
        engine_seq_steps(&eng, st.step);
        mailbox_status_publish(&mb, &st);

        (void)alsa_write(g_pcm, block, PERIOD); /* -1 = dropped (counted) */
    }
    return NULL;
}

/* One-line delta summary of the draft vs factory defaults (smoke aid). */
static void log_commit(const ui_t *ui)
{
    if (!g_log)
        return;
    const synth_params_t *d = &ui->draft;
    printf("MAILBOX commit: run=%d A(%.1fHz %s lvl%d) B(%.1fHz) "
           "C(%.1fHz) filtA %.0fHz seqA %s bpm%d master %d%%\n",
           d->run, (double)d->chain[0].freq_hz,
           d->chain[0].wave == WAVE_SAW ? "saw" : "?",
           d->chain[0].level, (double)d->chain[1].freq_hz,
           (double)d->chain[2].freq_hz, (double)d->chain[0].fc,
           d->seq[0].on ? "on" : "off", d->bpm, d->master);
    fflush(stdout);
}

int main(void)
{
    g_log = getenv("OPHIS_UI_LOG") != NULL;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Load the OS's own controller databases — AUTHORITATIVE, no bundled
     * fallback (owner order 2026-11-05: our bundled RG351-line fallback
     * was never device-verified and once replaced the device-correct
     * mapping SDL had already loaded, killing Start/Select). ArkOS /
     * PortMaster carry the numbers for this exact unit; every working
     * PortMaster port relies on these files. A later
     * SDL_GameControllerAddMapping for the same GUID replaces earlier
     * ones. Desktop uses the keyboard, so this is handheld-only. */
#if defined(TARGET_R36S)
    {
        static const char *const os_dbs[] = {
            "/opt/system/Tools/PortMaster/gamecontrollerdb.txt",
            "/opt/inttools/gamecontrollerdb.txt",
        };
        for (int p = 0; p < (int)(sizeof os_dbs / sizeof os_dbs[0]); p++) {
            FILE *f = fopen(os_dbs[p], "r");
            if (!f)
                continue;
            char line[512];
            while (fgets(line, sizeof line, f)) {
                if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
                    continue;
                SDL_GameControllerAddMapping(line);
            }
            fclose(f);
        }
    }
    /* Explicitly open any attached gamepad so events flow even if the
     * mapping was registered after the device was already present. */
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            g_ctrl = SDL_GameControllerOpen(i);
            if (g_ctrl)
                fprintf(stderr, "opened controller %d: %s\n", i,
                        SDL_GameControllerNameForIndex(i));
            else
                fprintf(stderr, "open controller %d failed: %s\n", i,
                        SDL_GetError());
        }
    }
    /* Fallback (PLAN_RG28XX_BUILD M3): firmware SDL2 with no mapping for
     * the pad — register one from the runtime GUID and retry. Raw
     * indices measured on the device (owner test): printed A=0 B=1 Y=2
     * X=3, L1=4 R1=5 L2=6 R2=7, Select=8 Start=9 Menu=10 (the firmware
     * maps the top Menu key to BTN_START, so MENU opens on M); D-pad is
     * a hat (probe: buttons=15 hats=1). */
    if (!g_ctrl) {
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            SDL_Joystick *js = SDL_JoystickOpen(i);
            if (!js)
                continue;
            char guid[64];
            SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js),
                                      guid, (int)sizeof guid);
            fprintf(stderr,
                    "joystick %d: %s guid=%s buttons=%d hats=%d axes=%d\n",
                    i, SDL_JoystickName(js), guid,
                    SDL_JoystickNumButtons(js), SDL_JoystickNumHats(js),
                    SDL_JoystickNumAxes(js));
            char map[512];
            snprintf(map, sizeof map,
                     "%s,H700 Pad,a:b0,b:b1,x:b3,y:b2,"
                     "back:b8,start:b9,guide:b10,"
                     "leftshoulder:b4,rightshoulder:b5,"
                     "lefttrigger:b6,righttrigger:b7,"
                     "dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,"
                     "platform:Linux", guid);
            SDL_GameControllerAddMapping(map);
            if (SDL_IsGameController(i)) {
                g_ctrl = SDL_GameControllerOpen(i);
                fprintf(stderr, "fallback controller %d: %s (%s)\n", i,
                        SDL_GameControllerNameForIndex(i),
                        g_ctrl ? "ok" : SDL_GetError());
            }
            SDL_JoystickClose(js);
            if (g_ctrl)
                break;
        }
    }
#endif
    SDL_Window *win = SDL_CreateWindow("OPHIS-DRONE",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640 * UI_SCALE, 480 * UI_SCALE,
        KB_FULLSCREEN ? SDL_WINDOW_FULLSCREEN : 0);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
#if defined(TARGET_R36S)
    /* RG28XX portrait panel (PLAN_RG28XX_BUILD M3): the SDL output is
     * 480x640, so the 640x480 UI renders into an offscreen texture and
     * is presented rotated. OPHIS_ROT=90 flips the direction (default
     * 270 = CCW quarter turn). R36S panels are native landscape: NULL
     * scene = direct rendering, unchanged. */
    SDL_Texture *scene = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, 640, 480);
    int rot = 270;
    {
        const char *rv = getenv("OPHIS_ROT");
        if (rv && strcmp(rv, "90") == 0)
            rot = 90;
    }
    if (!scene) {
        fprintf(stderr, "scene texture failed: %s\n", SDL_GetError());
        SDL_RenderSetLogicalSize(ren, 640, 480);
    } else {
        fprintf(stderr, "rotation: %d\n", rot);
    }
#else
    SDL_RenderSetLogicalSize(ren, 640, 480);
#endif

#if defined(TARGET_R36S)
    /* Rotation diagnostics (PLAN_RG28XX_BUILD M3): the RG28XX panel is
     * natively portrait — record what SDL actually presents before any
     * rotation decision. */
    {
        SDL_DisplayMode dm;
        int ow = 0, oh = 0;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
            fprintf(stderr, "display mode: %dx%d driver=%s\n",
                    dm.w, dm.h, SDL_GetCurrentVideoDriver());
        if (SDL_GetRendererOutputSize(ren, &ow, &oh) == 0)
            fprintf(stderr, "renderer output: %dx%d\n", ow, oh);
    }
#endif

    mailbox_init(&mb);
    static ui_t ui;                          /* big object, not on the stack */
    ui_init(&ui, &mb);
    mailbox_write(&mb, &ui.draft);           /* initial commit */
    log_commit(&ui);

    pthread_t th;
    if (pthread_create(&th, NULL, audio_thread_fn, NULL) != 0) {
        fprintf(stderr, "pthread_create failed\n");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    while (!atomic_load(&audio_ready) && !atomic_load(&quit))
        SDL_Delay(1);

    ui_status_t st;
    memset(&st, 0, sizeof st);
    Uint32 last_refresh = 0;
    /* Optional smoke auto-quit (env, ms); exit remains menu-only otherwise.
     * P3 (PLAN_CODE_REVIEW_FIXES.md Phase 6): strtol with validation —
     * junk rejected / range clamped in autoquit_parse, not bare atoi. */
    Uint32 autoquit = 0, t0 = SDL_GetTicks();
    {
        const char *aq = getenv("OPHIS_AUTOQUIT_MS");
        unsigned long ms = 0;
        if (aq && autoquit_parse(aq, &ms))
            autoquit = (Uint32)ms;
    }
    while (!atomic_load(&quit)) {
        SDL_Event e;
        int dirty = 0;
        while (SDL_PollEvent(&e)) {
            ui_action_t act;
            /* U2 (see ui_event_filter in ui.c — S3 moved the policy
             * there): text entry owns the keyboard while naming. */
            int mapped = 0;
            if (ui_event_filter(&ui, &e))    /* S3: policy lives in the UI */
                mapped = keymap_action(&e, &act);
            if (mapped) {
                ui_handle_action(&ui, act);
                log_commit(&ui);
                dirty = 1;
            } else {
                ui_handle_event(&ui, &e);
                if (e.type == SDL_QUIT)
                    atomic_store(&quit, 1);
                else if (e.type != SDL_KEYUP &&
                         e.type != SDL_TEXTINPUT)
                    dirty = 1;
            }
        }
        if (SDL_GetTicks() - last_refresh >= REFRESH_MS) {
            /* <= 10 Hz refresh; H2: a 0 return means the host published
             * mid-read — keep the last good status (skip this redraw) */
            if (mailbox_status_read(&mb, &st))
                dirty = 1;
            last_refresh = SDL_GetTicks();
        }
        ui_tick(&ui, SDL_GetTicks());
        if (ui_wants_quit(&ui))
            atomic_store(&quit, 1);
        if (autoquit && SDL_GetTicks() - t0 >= autoquit)
            atomic_store(&quit, 1);
        if (dirty) {
#if defined(TARGET_R36S)
            if (scene)
                SDL_SetRenderTarget(ren, scene);
#endif
            ui_draw(&ui, ren, &st);          /* event-driven redraw */
#if defined(TARGET_R36S)
            if (scene) {
                /* Present rotated with the pre-rotation bounding box as
                 * dst: after the quarter turn the quad covers the output
                 * exactly. NULL dst would stretch the texture into the
                 * portrait rect first (squeeze + bars + crop). */
                SDL_Rect rotated_fit = { -80, 80, 640, 480 };
                SDL_SetRenderTarget(ren, NULL);
                SDL_RenderCopyEx(ren, scene, NULL, &rotated_fit, (double)rot,
                                 NULL, SDL_FLIP_NONE);
                SDL_RenderPresent(ren);
            }
#endif
        }
        SDL_Delay(5);
    }

    atomic_store(&quit, 1);
    pthread_join(th, NULL);

    long x = g_pcm ? alsa_xruns(g_pcm) : -1;
    if (g_pcm)
        alsa_close(g_pcm);
#if defined(TARGET_R36S)
    if (g_ctrl)
        SDL_GameControllerClose(g_ctrl);
    if (scene)
        SDL_DestroyTexture(scene);
#endif
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("XRUNS %ld\n", x);           /* LAST line — desktop gate: 0 */
    return 0;
}
