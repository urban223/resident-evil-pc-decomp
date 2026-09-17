// Achievements.cpp - CUSTOM (port-only): the achievement toast.
//
// See Achievements.h for what this is and why it bypasses the PSX 2D path.
//
// Layout, in atlas pixels (the generator bakes the panel at 2x its design
// size, and the runtime draws one atlas pixel as `k` backbuffer pixels, so
// these numbers are the toast's own coordinate system):
//
//     +----------------------------------------------------------+
//     |  +------+   ACHIEVEMENT UNLOCKED        <- label, y 8     |
//     |  | icon |   FIRST BLOOD                 <- name,  y 26    |
//     |  +------+   Killed your first enemy     <- sub,   y 60    |
//     +----------------------------------------------------------+   92
//     0         96                                               500
//
// A progress toast replaces the sub line with the bar at y 66 and its
// "12 / 25" counter to the right of it.
//
// The toast arrives in two movements: a narrow capsule - just the icon slot
// and its plate, ACHV_PANEL_CAP_L + ACHV_PANEL_CAP_R wide - drops in from the
// top edge, and then the plate opens out to its full width to the RIGHT of
// that capsule, so the icon never moves once it has landed. Leaving is the
// same in reverse: the plate folds back down to the capsule, then the capsule
// rises out of the top. That is what the panel's horizontal 3-slice is for -
// the caps hold the cut corners and all four brackets, and only the flat
// middle is stretched, so the frame never distorts at any width.
//
// Sound/achv.wav plays as the capsule starts to fall. Only an UNLOCK toast
// makes a sound: a progress step is an interim note, and a cue every fifth of
// a counter turns the reward into a nag - the payoff belongs to the moment the
// thing is actually finished.
#include "Achievements.h"
#include "AchievementAtlasData.h"
#include "FileLoader.h"
#include "../marni/MarniSystem.h"
#include "../marni/MarniSound.h"
#include "../system/AssetPath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// The achievement table. `target` 0 means a plain unlock; anything else is a
// counter, and the toast then carries a progress bar.
// ---------------------------------------------------------------------------
struct AchvDef {
    const char* name;
    const char* sub;
    short       icon;
    short       target;
    short       points;     // what the status screen's POINTS field totals
};

static const AchvDef s_defs[ACHV_COUNT] = {
    { "FIRST BLOOD",      "Killed your first enemy",           ACHV_ICON_SKULL,  0,  50  },
    { "KEEPING A RECORD", "Saved the game for the first time", ACHV_ICON_PENCIL, 0,  50  },
    { "EXTERMINATOR",     "Kill 25 enemies",                   ACHV_ICON_MEDAL,  25, 150 },
};

// ---------------------------------------------------------------------------
// Saved profile: <save root>/achieve.dat
// ---------------------------------------------------------------------------
#define ACHV_FILE_MAGIC   0x31484341u   // 'ACH1'

static unsigned char s_unlocked[ACHV_COUNT];
static int           s_counter[ACHV_COUNT];
static int           s_profileLoaded = 0;

static void achv_profile_path(char* out, int size)
{
    snprintf(out, (size_t)size, "%sachieve.dat", GetSaveRoot());
}

static void achv_profile_load(void)
{
    if (s_profileLoaded) return;
    s_profileLoaded = 1;
    memset(s_unlocked, 0, sizeof(s_unlocked));
    memset(s_counter, 0, sizeof(s_counter));

    char path[260];
    achv_profile_path(path, sizeof(path));
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) return;                 // first run: nothing unlocked yet

    unsigned int header[2] = { 0, 0 };
    if (fread(header, sizeof(unsigned int), 2, fp) == 2 && header[0] == ACHV_FILE_MAGIC) {
        // A file written by an older build is shorter than this one expects;
        // read the entries it does have and leave the rest at zero.
        unsigned int count = header[1];
        if (count > (unsigned int)ACHV_COUNT) count = (unsigned int)ACHV_COUNT;
        if (count > 0) {
            fread(s_unlocked, 1, count, fp);
            fread(s_counter, sizeof(int), count, fp);
        }
    }
    fclose(fp);
}

static void achv_profile_save(void)
{
    char path[260];
    achv_profile_path(path, sizeof(path));
    FILE* fp = fopen(path, "wb");
    if (fp == NULL) return;                 // read-only save folder: stay quiet

    unsigned int header[2] = { ACHV_FILE_MAGIC, (unsigned int)ACHV_COUNT };
    fwrite(header, sizeof(unsigned int), 2, fp);
    fwrite(s_unlocked, 1, ACHV_COUNT, fp);
    fwrite(s_counter, sizeof(int), ACHV_COUNT, fp);
    fclose(fp);
}

// ---------------------------------------------------------------------------
// Toast queue and animation
//
// Frame counts are ticks of the game's fixed 30 Hz presentation, the same
// clock FrameRateGovernor runs on.
// ---------------------------------------------------------------------------
#define ACHV_KIND_UNLOCK    0
#define ACHV_KIND_PROGRESS  1

#define ACHV_QUEUE_MAX      8

// Animation phases, in presented frames.
#define ACHV_PHASE_IDLE     0
#define ACHV_PHASE_DROP     1   // the icon capsule drops in from the top edge
#define ACHV_PHASE_OPEN     2   // the plate opens out to full width
#define ACHV_PHASE_HOLD     3
#define ACHV_PHASE_CLOSE    4   // ...and folds back down to the capsule
#define ACHV_PHASE_RISE     5   // which then leaves the way it came

#define ACHV_FRAMES_DROP    8
#define ACHV_FRAMES_OPEN    7
#define ACHV_FRAMES_HOLD    78
#define ACHV_FRAMES_CLOSE   6
#define ACHV_FRAMES_RISE    8

// The text only exists once there is a plate to put it on, so it fades in over
// the first frames of the hold rather than being squeezed by the opening.
#define ACHV_FRAMES_TEXT    5

struct AchvToast {
    short id;
    short kind;
    int   value;     // counter value at the moment the toast was queued
};

static AchvToast s_queue[ACHV_QUEUE_MAX];
static int s_queueHead = 0;
static int s_queueCount = 0;

static int s_phase = ACHV_PHASE_IDLE;
static int s_timer = 0;

static void achv_queue_push(int id, int kind, int value)
{
    if (s_queueCount >= ACHV_QUEUE_MAX) return;   // a flood drops the extras
    int slot = (s_queueHead + s_queueCount) % ACHV_QUEUE_MAX;
    s_queue[slot].id = (short)id;
    s_queue[slot].kind = (short)kind;
    s_queue[slot].value = value;
    s_queueCount++;
}

// ---------------------------------------------------------------------------
// Sound
//
// ONE cue, Sound/achv.wav, and only an unlock plays it. A progress toast is
// deliberately silent: it is an interim note, and a chime every fifth of a
// counter makes the reward into a nag. Finishing a counter arrives here as an
// ordinary unlock (Achievements_AddProgress calls Achievements_Unlock when the
// target is reached), so the sound lands on completion for counters too.
//
// The cue is this feature's own file, loaded into its own bank and kept for
// the life of the process. Borrowing a gameplay SFX instead would mean
// depending on a bank that is only loaded in some rooms - an achievement can
// fire anywhere - and the room loader would be free to throw it away between
// toasts. A bank allocated here is never one the game's own code destroys: it
// only ever destroys ids it allocated itself (sounds_reset and its siblings
// walk their own tables).
// ---------------------------------------------------------------------------

// 0x00... (Globals.h) - the options screen's SFX volume, in DirectSound
// hundredths of a decibel: 0 is full scale, -10000 silence. The toast rides it
// so that turning the game's effects down turns the toast down with them.
extern int g_SfxVolume;

#define ACHV_SND_TRIM       (-100)   // -1 dB under the game's own effects
#define ACHV_SND_MAX_TRIES  3

static int s_sndBank = 0;
static int s_sndTries = 0;

static void achv_sound_play(int kind)
{
    if (kind != ACHV_KIND_UNLOCK) return;

    // The sound device is not up yet on the first frames, and
    // loadSndBankFromWav just answers 0 in that case, so retry on the next
    // toast rather than giving up for the session - but only a few times, so a
    // missing file does not reopen itself forever.
    if (s_sndBank == 0 && s_sndTries < ACHV_SND_MAX_TRIES) {
        s_sndTries++;
        s_sndBank = loadSndBankFromWav(GAME_DATA_ROOT "Sound\\achv.wav");
    }
    if (s_sndBank == 0) return;

    set_volume(s_sndBank, g_SfxVolume + ACHV_SND_TRIM);  // clamps to -9999..-1
    pan_set(s_sndBank, 0);   // dead centre: this is UI, not a sound in the room
    playSnd(s_sndBank, 0);
}

// ---------------------------------------------------------------------------
// Atlas
// ---------------------------------------------------------------------------
static MarniHandle s_atlas = MARNI_NULL_HANDLE;
static int s_atlasTried = 0;

static void achv_atlas_load(void)
{
    if (s_atlas != MARNI_NULL_HANDLE || s_atlasTried) return;
    if (!IsGraphicsSystemReadyForOperation()) return;   // retry next frame
    s_atlasTried = 1;

    const size_t pixels = (size_t)ACHV_ATLAS_W * (size_t)ACHV_ATLAS_H * 4;
    const size_t expect = pixels + 12;                  // 'AUI1' + w + h
    unsigned char* buf = (unsigned char*)malloc(expect);
    if (buf == NULL) return;

    size_t read = LoadFile(GAME_DATA_ROOT "Data\\achvui.bin", buf, 0);
    if (read == expect
        && buf[0] == 'A' && buf[1] == 'U' && buf[2] == 'I' && buf[3] == '1') {
        unsigned int w = *(unsigned int*)(buf + 4);
        unsigned int h = *(unsigned int*)(buf + 8);
        if (w == (unsigned int)ACHV_ATLAS_W && h == (unsigned int)ACHV_ATLAS_H) {
            // bpp 32 is memcpy'd straight into an R8G8B8A8 texture
            // (MarniDX::CreateTexture), so the file's byte order IS the
            // texture's: R, G, B, A per pixel, which is what the generator
            // writes.
            MarniCreateTexture(ACHV_ATLAS_W, ACHV_ATLAS_H, 32, buf + 12, &s_atlas);
        }
    }
    free(buf);
}

// ---------------------------------------------------------------------------
// Drawing helpers. Every coordinate here is in backbuffer pixels; `k` converts
// atlas pixels to those.
// ---------------------------------------------------------------------------
static float achv_ui_scale(void)
{
    // The toast is sized off the real backbuffer, not the game's 320x240
    // logical space, so it stays the same fraction of the window at every
    // resolution instead of growing into a 1/3-screen slab at 4K.
    DWORD bw = 0, bh = 0;
    MarniGetBackBufferSize(&bw, &bh);
    if (bh < 240) bh = 240;
    float k = ((float)bh / 480.0f) * 0.5f;
    if (k < 0.45f) k = 0.45f;
    if (k > 2.20f) k = 2.20f;
    return k;
}

static void achv_blit(const AchvRect* r, float x, float y, float w, float h,
                      unsigned int color)
{
    const float u0 = (float)r->x / (float)ACHV_ATLAS_W;
    const float v0 = (float)r->y / (float)ACHV_ATLAS_H;
    const float u1 = (float)(r->x + r->w) / (float)ACHV_ATLAS_W;
    const float v1 = (float)(r->y + r->h) / (float)ACHV_ATLAS_H;
    MarniDrawSpriteEx(x, y, w, h, u0, v0, u1, v1, color, s_atlas,
                      MARNI_SAMPLER_LINEAR, MARNI_BLEND_ALPHA);
}

static const PortFont s_achvFontTitle = { g_achvFontTitle, ACHV_FONT_FIRST, ACHV_FONT_LAST };
static const PortFont s_achvFontBody  = { g_achvFontBody,  ACHV_FONT_FIRST, ACHV_FONT_LAST };

static void achv_glyph_blit(void* user, const PortGlyph* g,
                            float x, float y, float w, float h)
{
    AchvRect r;
    r.x = g->x; r.y = g->y; r.w = g->w; r.h = g->h;
    achv_blit(&r, x, y, w, h, *(const unsigned int*)user);
}

static void achv_draw_text(const PortFont* font, const char* s,
                           float x, float y, float k, unsigned int color)
{
    PortText_Draw(font, s, x, y, k, achv_glyph_blit, &color);
}

static void achv_fill(float x, float y, float w, float h, unsigned int color)
{
    achv_blit(&g_achvWhite, x, y, w, h, color);
}

// alpha 0..255 folded into an 0xAARRGGBB literal
static unsigned int achv_fade(unsigned int argb, int alpha)
{
    unsigned int a = ((argb >> 24) & 0xFF) * (unsigned int)alpha / 255u;
    return (a << 24) | (argb & 0x00FFFFFFu);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void Achievements_Unlock(int id)
{
    if (id < 0 || id >= ACHV_COUNT) return;
    achv_profile_load();
    if (s_unlocked[id]) return;

    s_unlocked[id] = 1;
    const AchvDef* def = &s_defs[id];
    if (def->target > 0 && s_counter[id] < def->target) {
        s_counter[id] = def->target;
    }
    achv_profile_save();
    achv_queue_push(id, ACHV_KIND_UNLOCK, s_counter[id]);
}

void Achievements_AddProgress(int id, int amount)
{
    if (id < 0 || id >= ACHV_COUNT || amount <= 0) return;
    achv_profile_load();
    if (s_unlocked[id]) return;

    const AchvDef* def = &s_defs[id];
    if (def->target <= 0) { Achievements_Unlock(id); return; }

    const int before = s_counter[id];
    int now = before + amount;
    if (now > def->target) now = def->target;
    s_counter[id] = now;

    if (now >= def->target) {
        Achievements_Unlock(id);             // saves the profile itself
        return;
    }

    achv_profile_save();

    // One progress toast per fifth of the target - enough to feel like
    // feedback, not so many that killing things becomes a slide show.
    int step = def->target / 5;
    if (step < 1) step = 1;
    if (before / step != now / step) {
        achv_queue_push(id, ACHV_KIND_PROGRESS, now);
    }
}

void Achievements_OnEnemyKilled(void)
{
    Achievements_Unlock(ACHV_FIRST_BLOOD);
    Achievements_AddProgress(ACHV_EXTERMINATOR, 1);
}

void Achievements_OnGameSaved(void)
{
    Achievements_Unlock(ACHV_KEEPING_A_RECORD);
}

static int achv_phase_length(int phase)
{
    switch (phase) {
    case ACHV_PHASE_DROP:  return ACHV_FRAMES_DROP;
    case ACHV_PHASE_OPEN:  return ACHV_FRAMES_OPEN;
    case ACHV_PHASE_HOLD:  return ACHV_FRAMES_HOLD;
    case ACHV_PHASE_CLOSE: return ACHV_FRAMES_CLOSE;
    case ACHV_PHASE_RISE:  return ACHV_FRAMES_RISE;
    default:               return 0;
    }
}

void Achievements_Tick(void)
{
    if (s_phase == ACHV_PHASE_IDLE) {
        if (s_queueCount == 0) return;
        s_phase = ACHV_PHASE_DROP;
        s_timer = 0;
        achv_sound_play(s_queue[s_queueHead].kind);
        return;
    }

    s_timer++;
    if (s_timer < achv_phase_length(s_phase)) return;

    s_timer = 0;
    if (s_phase == ACHV_PHASE_RISE) {
        // Done with this one: drop it and let the next frame start the next.
        s_phase = ACHV_PHASE_IDLE;
        s_queueHead = (s_queueHead + 1) % ACHV_QUEUE_MAX;
        if (s_queueCount > 0) s_queueCount--;
    } else {
        s_phase++;
    }
}

// The plate as three horizontal slices: the two caps keep their cut corners
// and brackets at their baked size whatever the toast's current width is, and
// only the flat middle between them is stretched. `w` is clamped so the caps
// can never overlap - at the minimum the two caps ARE the toast, which is the
// collapsed icon capsule.
static void achv_draw_plate(float x, float y, float w, float h, float k,
                            unsigned int color)
{
    const float capL = (float)ACHV_PANEL_CAP_L * k;
    const float capR = (float)ACHV_PANEL_CAP_R * k;

    AchvRect r;
    r.y = g_achvPanel.y;
    r.h = g_achvPanel.h;

    const float mid = w - capL - capR;
    if (mid > 0.0f) {
        r.x = (short)(g_achvPanel.x + ACHV_PANEL_CAP_L);
        r.w = (short)(g_achvPanel.w - ACHV_PANEL_CAP_L - ACHV_PANEL_CAP_R);
        achv_blit(&r, x + capL, y, mid, h, color);
    }

    r.x = g_achvPanel.x;
    r.w = (short)ACHV_PANEL_CAP_L;
    achv_blit(&r, x, y, capL, h, color);

    r.x = (short)(g_achvPanel.x + g_achvPanel.w - ACHV_PANEL_CAP_R);
    r.w = (short)ACHV_PANEL_CAP_R;
    achv_blit(&r, x + w - capR, y, capR, h, color);
}

void Achievements_Draw(void)
{
    if (s_phase == ACHV_PHASE_IDLE || s_queueCount == 0) return;

    achv_atlas_load();
    if (s_atlas == MARNI_NULL_HANDLE) return;

    const AchvToast* toast = &s_queue[s_queueHead];
    if (toast->id < 0 || toast->id >= ACHV_COUNT) return;
    const AchvDef* def = &s_defs[toast->id];

    // --- how far through the current phase -------------------------------
    const int len = achv_phase_length(s_phase);
    float t = (len > 0) ? (float)s_timer / (float)len : 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const float invT = 1.0f - t;
    const float easeOut = 1.0f - invT * invT * invT;   // fast, then settling
    const float easeIn  = t * t * t;                   // slow, then away

    // --- vertical travel and fade: only the capsule moves ------------------
    // 1 = landed, 0 = fully off the top edge.
    float drop = 1.0f;
    if (s_phase == ACHV_PHASE_DROP)      drop = easeOut;
    else if (s_phase == ACHV_PHASE_RISE) drop = 1.0f - easeIn;

    // --- horizontal opening: 0 = icon capsule, 1 = full plate ---------------
    float open = 0.0f;
    if (s_phase == ACHV_PHASE_OPEN)       open = easeOut;
    else if (s_phase == ACHV_PHASE_HOLD)  open = 1.0f;
    else if (s_phase == ACHV_PHASE_CLOSE) open = 1.0f - easeIn;

    DWORD bw = 0, bh = 0;
    MarniGetBackBufferSize(&bw, &bh);
    if (bw < 320) bw = 320;

    const float k = achv_ui_scale();
    const float fullW = (float)g_achvPanel.w * k;
    const float capsuleW = (float)(ACHV_PANEL_CAP_L + ACHV_PANEL_CAP_R) * k;
    const float ph = (float)g_achvPanel.h * k;

    const float pw = capsuleW + (fullW - capsuleW) * open;
    // The plate opens to the RIGHT of the capsule: its left edge is already
    // where the finished toast's left edge will be, so the icon lands once and
    // stays put instead of sliding out from under itself.
    const float px = ((float)bw - fullW) * 0.5f;
    const float py = -ph + (ph + 12.0f * k) * drop;   // 12 atlas px of top margin
    const int   alpha = (int)(drop * 255.0f);

    // --- plate, icon slot, icon ---
    achv_draw_plate(px, py, pw, ph, k, achv_fade(0xFFFFFFFFu, alpha));
    achv_blit(&g_achvIconSlot, px + 14.0f * k, py + 14.0f * k, 64.0f * k, 64.0f * k,
              achv_fade(0xBE2EC4B6u, alpha));
    if (def->icon >= 0 && def->icon < ACHV_ICON_COUNT) {
        achv_blit(&g_achvIcons[def->icon], px + 22.0f * k, py + 22.0f * k,
                  48.0f * k, 48.0f * k, achv_fade(0xFFBEFAF4u, alpha));
    }

    // --- text, only once the plate is open ---------------------------------
    // There is no scissor rectangle in this draw path, so text drawn while the
    // plate is still opening would hang off its right edge. It fades in over
    // the first frames of the hold instead, which also reads as the panel
    // settling.
    if (s_phase != ACHV_PHASE_HOLD) return;
    int textAlpha = 255;
    if (s_timer < ACHV_FRAMES_TEXT) {
        textAlpha = (s_timer * 255) / ACHV_FRAMES_TEXT;
    }

    const float tx = px + 96.0f * k;
    const char* label = (toast->kind == ACHV_KIND_PROGRESS) ? "PROGRESS"
                                                            : "ACHIEVEMENT UNLOCKED";
    achv_draw_text(&s_achvFontBody, label, tx, py + 8.0f * k, k,
                   achv_fade(0xFF56AAA6u, textAlpha));
    achv_draw_text(&s_achvFontTitle, def->name, tx, py + 26.0f * k, k,
                   achv_fade(0xFFE4FFFCu, textAlpha));

    if (toast->kind == ACHV_KIND_PROGRESS && def->target > 0) {
        const float barW = 290.0f * k;
        const float barX = tx;
        const float barY = py + 66.0f * k;
        const float barH = 6.0f * k;
        int value = toast->value;
        if (value < 0) value = 0;
        if (value > def->target) value = def->target;

        achv_fill(barX, barY, barW, barH, achv_fade(0x462EC4B6u, textAlpha));
        achv_fill(barX, barY, barW * (float)value / (float)def->target, barH,
                  achv_fade(0xFFC6FF4Au, textAlpha));

        char count[32];
        snprintf(count, sizeof(count), "%d / %d", value, (int)def->target);
        achv_draw_text(&s_achvFontBody, count, barX + barW + 12.0f * k,
                       py + 58.0f * k, k, achv_fade(0xFFC6FF4Au, textAlpha));
    } else {
        achv_draw_text(&s_achvFontBody, def->sub, tx, py + 60.0f * k, k,
                       achv_fade(0xFF7ABAB8u, textAlpha));
    }
}


// ---------------------------------------------------------------------------
// Read-only queries for the status screen's POINTS field and pip row.
// ---------------------------------------------------------------------------
int Achievements_Points(void)
{
    achv_profile_load();
    int total = 0;
    for (int i = 0; i < ACHV_COUNT; i++) {
        if (s_unlocked[i]) total += s_defs[i].points;
    }
    return total;
}

int Achievements_UnlockedCount(void)
{
    achv_profile_load();
    int n = 0;
    for (int i = 0; i < ACHV_COUNT; i++) n += (s_unlocked[i] != 0);
    return n;
}

int Achievements_Total(void)
{
    return ACHV_COUNT;
}
