// TitleScreen.cpp - Title screen rendering and state management
// All functions decompiled from Ghidra with original addresses
#include "../Globals.h"
#include "CoopPlayer.h"   // CUSTOM: RAID co-op
#include "CoopNet.h"      // CUSTOM: RAID co-op transport
#include "../marni/MarniSystem.h"
#include "../marni/MarniSound.h"
#include "../marni/PSXTexture.h"
#include "FileLoader.h"
#include "SpriteRenderer.h"
#include "SFXIds.h"
#include "UiAtlas.h"        // CUSTOM: the title menu's own word art
#include "../platform/platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "../system/AssetPath.h"

extern void logos_state(void);

// ============================================================================
// set_display_resolution (0x00401000)
// ============================================================================
void set_display_resolution(int w, int h, int mode)
{
    g_displayWidth = w;
    g_displayHeight = h;
    g_displayMode = mode;
}

// ============================================================================
// check_save_files_exist (0x00494190)
// Returns 1 if any save files exist, 0 otherwise.
// ============================================================================
int check_save_files_exist(void)
{
    char path[260];
    for (int i = 1; i <= 8; i++) {
        sprintf(path, "%ssavedat%d.dat", GetSaveRoot(), i);
        FILE* f = fopen(path, "rb");
        if (f != NULL) {
            fclose(f);
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// title_setup_texture_pages (0x00470970)
// Creates texture pages for button prompt images.
// ============================================================================
void title_setup_texture_pages(int slot, int mode)
{
    // Same legacy-descriptor caveat as TextureLoader: these tables are indexed
    // by slot * 0x37C and the port's stand-ins are a few KB, so the room-load
    // calls (load_room_bg passes the camera index, 0..7) run off the end. The
    // DX11 path takes page state from the g_TexturePage* arrays, so reads that
    // fall out of range can yield 0 - but the destroy loop's writes must not
    // happen at all.
    int slotBase = slot * 0x37C;
    const bool cntOk   = (size_t)slotBase / sizeof(DWORD)
                         < sizeof(g_VideoDriverArray_814) / sizeof(DWORD);
    const bool tableOk = (size_t)slotBase + 8 * sizeof(DWORD) <= sizeof(g_TexturePageTable_DAT);
    const bool dataOk  = (size_t)slotBase + 2 * 0x68 <= sizeof(g_VideoDriverArray_4d0);

    int pageCount = cntOk ? g_VideoDriverArray_814[slotBase / 4] : 0;
    if (pageCount != 0 && tableOk) {
        DWORD* pageTable = (DWORD*)((BYTE*)&g_TexturePageTable_DAT + slotBase);
        for (int i = 0; i < pageCount; i++) {
            if ((size_t)slotBase + (size_t)(i + 1) * sizeof(DWORD)
                > sizeof(g_TexturePageTable_DAT)) {
                break;
            }
            if (pageTable[i] != 0) {
                destroy_texture_page(pageTable[i]);
                pageTable[i] = 0;
            }
        }
    }

    if (dataOk) {
        BYTE* pageData = (BYTE*)&g_VideoDriverArray_4d0 + slotBase;
        for (int i = 0; i < 2; i++) {
            int handle = create_texture_page(pageData, (mode != 0) ? 26 : 10);
            if (handle == 0) break;
            pageData += 0x68;
        }
    }

    g_titleTextureSlotId = slot;
}

// ============================================================================
// init_title_screen (0x004306e0)
// ============================================================================
void init_title_screen(void)
{
    g_bGameActive = 0;
    set_display_resolution(320, 240, 0);

    g_roomCameraId = 0;

    // CUSTOM: titlebg.pix, not title.pix.  The stock file has the wordmark
    // burnt into the eye, so the picture and the name are the same pixels and
    // neither can be changed without the other.  tools/build_title_bg.py
    // splits them: this file is the room on its own, and the wordmark comes
    // back as a sprite in title_draw_logo().
    LoadFile(GAME_DATA_ROOT "data\\titlebg.pix", g_TimImageBuffer__bitmap, 0x20);
    display_image(0, g_TimImageBuffer__bitmap, 320, 240);

    title_setup_texture_pages(0, 1);

    //empty_00470960(0);

    const char* buttonTexPath;
    if (!g_bPadConnected) {
        buttonTexPath = GAME_DATA_ROOT "data\\t_press.tim";
    } else {
        buttonTexPath = GAME_DATA_ROOT "data\\t_start.tim";
    }
    LoadFile(buttonTexPath, g_TimImageBuffer__bitmap, 0x20);

    g_titleTexturePageData[4] = 26;
    g_titleTexturePageData[0] = 8;
    g_TextureCurrentPage = 26;
    g_TextureBankID = 8;
    LoadTexturePage(g_TimImageBuffer__bitmap, 8, 0, 12, 4, 0, 0, 0);

    g_titleTexturePageData[1] = g_TextureBankID;
    g_titleLoopFlag = 1;
    g_titleTexturePageData[5] = g_TextureCurrentPage;
    g_titleTexturePageData[2] = g_titleTexturePageData[1];
    g_titleTexturePageData[6] = g_titleTexturePageData[5];

    {
        char dbg[256];
        sprintf(dbg, "[INIT] LoadTexturePage done, SRV[27]=%p\n", g_TexturePageSRV[27]);
        OutputDebugStringA(dbg);
    }


    if (check_save_files_exist()) {
        g_titleSelectionId = 2;
        g_main_state_flags &= ~MSF_SCREEN_MODE_MASK;
        return;
    }
    g_titleSelectionId = 1;
    g_main_state_flags &= ~MSF_SCREEN_MODE_MASK;
}

// ============================================================================
// set_scene_render_param (0x0040a8e0)
// ============================================================================
void set_scene_render_param(int value)
{
    g_sceneRenderParam = value;
}

// ============================================================================
// title_exit_loop (0x00430e10)
// ============================================================================
void title_exit_loop(void)
{
    g_titleLoopFlag = 0;
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
}

// ============================================================================
// fade_update (0x0047b950)
// ============================================================================
void fade_update(void)
{
    if (g_fading_state <= 0 && g_fading_counter != 0) {
        if (g_fading_counter <= 0) {
            g_fading_state = 0x7FFF;
        } else {
            g_fading_state = 0;
        }
    }
}

// ============================================================================
// read_sidewinder_pad (0x00497e30)
// Original: return g_pMasterInputState.field466_0x200 (joystick[0].currPress)
// ============================================================================
int read_sidewinder_pad(void)
{
    return g_pMasterInputState.joysticks[0].currPress;
}

// ============================================================================
// TitleTextPosData and UpdateTitleTextSprite
// ============================================================================
struct TitleTextPosData {
    int vramY;
    int sprHeight;
    int screenY;
};

static const TitleTextPosData g_titleTextPosTable[3] = {
    { 0,  54, 24 },  // index 0: "PRESS ANY BUTTON"
    { 83, 70,  8 },  // index 1: "NEW GAME"
    { 175,70,  8 },  // index 2: "LOAD GAME"
};

// ============================================================================
// UpdateTitleTextSprite (0x00430d40)
// ============================================================================
void UpdateTitleTextSprite(unsigned char brightness, unsigned char selectionId)
{
    TextureDesc* td = &g_TextureDesc;

    td->flags = 0x10000000;
    if (brightness != 0x80) {
        td->flags = 0x40000000;
    }

    const TitleTextPosData* entry = &g_titleTextPosTable[selectionId];

    td->texU = 0;
    td->screenX = -130;
    td->texturePage = g_titleTexturePageData[selectionId];
    td->width = 256;
    td->texV = (unsigned char)entry->vramY;
    td->height = entry->sprHeight;
    td->screenY = entry->screenY + 38;
    // g_titleCurrentSprH = (float)entry->sprHeight;

    td->colorMulR = brightness;
    td->clutX = 0;
    td->colorMulG = brightness;
    td->pivotX = 0;
    td->pivotY = 0;
    td->colorMulB = brightness;

    td->clutY = 0x1E0;

    display_texture(td, 2, 12, 1);
}

// ============================================================================
// CUSTOM (port-only): the title menu.
//
// The original screen is two pre-composed bitmaps - one per highlight state -
// each holding NEW GAME, LOAD GAME and the copyright lines together, centred.
// There is no way to move one item, and no art at all for anything else. So the
// three pieces were lifted out of t_start.tim pixel for pixel at bake time and
// live in the shared atlas (AchievementAtlasData.h), where each can be placed
// on its own; EXTRA and QUIT are lettered to match, same cap height, same
// tracking. All four go through one draw path, which is what keeps them
// looking like a single menu rather than two fonts sharing a screen.
// ============================================================================

#define TITLE_ITEM_NEW   0
#define TITLE_ITEM_LOAD  1
#define TITLE_ITEM_EXTRA 2
#define TITLE_ITEM_QUIT  3
#define TITLE_ITEM_COUNT 4

// Left-aligned column. x is the shared left edge of every word - the point of
// the layout - and the rows are evenly spaced above the copyright line.
//
// These are in the ORIGINAL's coordinate space, which is centre-relative: its
// own 256-wide block sits at screenX -130 and lands centred, so the screen
// offset this space is measured from is the middle of a 320-wide screen. Using
// the same space (and the same "+ g_ScreenOffsetX" the engine's own 2D applies)
// means the menu cannot drift away from the art behind it, whatever that offset
// turns out to be - the numbers are anchored to the original block's own -130,
// not to an assumption about where the origin sits.
// The column sits in the lower half but well clear of the copyright, which
// keeps the place it had inside the original block. It no longer has to dodge
// the logo: the menu gets a background of its own (see title_menu_backdrop).
#define TITLE_MENU_X     (-132)   // 28 px in from the left edge
#define TITLE_MENU_Y     (-48)    // first row at y 72
#define TITLE_MENU_STEP  (24)
#define TITLE_COPY_X     (-128)   // the 256-wide copyright block, centred
#define TITLE_COPY_Y     (96)

static int s_titleMenuIndex = 0;

// CUSTOM: the menu gets its own backdrop. The title art is a full-bleed logo
// over an eye - fine for PRESS ANY BUTTON, but it leaves nowhere for a list to
// live. sel_back is the game's own file-select plate: the same world, dark,
// no lettering of its own, and empty on the left where the column goes.
//
// display_image replaces the framebuffer the renderer composites the screen
// from, so this is a single call at the moment of the transition, not
// something to repeat per frame.
static void title_menu_backdrop(void)
{
    LoadFile(GAME_DATA_ROOT "data\\SEL_BACK.PIX", g_TimImageBuffer__bitmap, 0x20);
    display_image(0, g_TimImageBuffer__bitmap, 320, 240);
}

// CUSTOM: the EXTRA screen. A heading and a prompt on black, which the menu
// leaves for and comes back from - it is not a mode of the menu, so it gets a
// title mode of its own rather than another step in the menu's fade counter.
#define TITLE_MODE_EXTRA 2

// CUSTOM: a title exit of its own. The original's ids are 0 attract demo,
// 1 character select, 2/3 load a save - all handled by title_state's switch.
// RAID needs neither a character select (it picks its own) nor a save load,
// so it gets an id that chains game_start directly.
#define TITLE_SEL_RAID   4

// Centre-relative space, the same one the menu is laid out in (see
// TITLE_MENU_X): y 0 is the middle of the screen. The heading sits above
// centre, clear of the native prompt, which draws itself at y 62 in this space.
#define TITLE_EXTRA_Y    (-40)

// 0 not on it / 1 the menu is fading out / 2 on it / 3 it is fading out
static int s_titleExtraPhase = 0;
static int s_titleExtraGuard = 0;

// Half again as slow as the reference. What works at 60 fps on a 1080p screen
// reads as a twitch at 30 on a 320x240 one, and this game's own pace is a slow
// one - the title's phrase alone runs three and a half seconds.
#define TITLE_EXTRA_F_LINE    48   // the rule opens out of the centre
#define TITLE_EXTRA_F_WORD    60   // RAID rises - the sting's hit lands here
#define TITLE_EXTRA_F_FLARE   78   // the light comes up behind it
#define TITLE_EXTRA_F_PROMPT  90   // the prompt fades up

static float title_ease_out(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    t = 1.0f - t;
    return 1.0f - t * t * t;
}

// alpha 0..255 into an 0xAARRGGBB colour
// Fade a colour towards white. k is how much of it is left: 1 keeps it, 0 is
// white. Per channel, so a colour that is already at full in one channel simply
// stays there.
static unsigned int title_warm(unsigned int rgb, float k)
{
    if (k >= 1.0f) return rgb;
    if (k < 0.0f) k = 0.0f;

    unsigned int out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        const int c = (int)((rgb >> sh) & 0xFF);
        int v = 255 - (int)((float)(255 - c) * k);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        out |= (unsigned int)v << sh;
    }
    return out;
}

static unsigned int title_rgba(unsigned int rgb, int a)
{
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return ((unsigned int)a << 24) | rgb;
}

// One bar of one shot, as the slice [t0, t1] of the ray that runs from the
// vanishing point V through the bar's flat anchor A. t is 0 at V and 1 at A,
// so the same two numbers mean "how far out it starts" and "how far out it
// reaches" in every framing, and a bar grows by pushing t1 out and clears by
// pulling t0 after it.
// The backdrop: the eye, whole, looking around.
//
// It is a baked file rather than a crop of title.pix, because title.pix cannot
// give a clean eye - the RESIDENT EVIL logo is across the middle of it with a
// shadow under every stroke, and the strokes are too wide to reconstruct what
// is behind them. The eye without the logo is in the game's own opening,
// ou.avi, one frame before the logo arrives; tools/build_raid_eye.py lifts it
// and does the framing, the dimming, the vignette and the scanlines there,
// since none of that moves.
//
// TWO layers. The eyeball holds still and the IRIS moves on it, because that is
// what an eye does - sliding the whole picture instead reads as the camera
// drifting, which is a different thing entirely. The baker cuts the iris out as
// its own sprite and fills the hole behind it by extending the sclera inwards;
// only a thin crescent of that fill is ever uncovered, on the side the iris
// moves away from.
#define TITLE_EYE_W       320        // must match build_raid_eye.py
#define TITLE_EYE_H       240
#define TITLE_EYE_DEPTH   1000u
#define TITLE_EYE_IRIS_D  999u       // one nearer, so it sits on the eyeball

static MarniHandle s_titleEye = MARNI_NULL_HANDLE;
static MarniHandle s_titleIris = MARNI_NULL_HANDLE;
static int s_titleIrisW = 0, s_titleIrisH = 0, s_titleIrisX = 0, s_titleIrisY = 0;
static int s_titleEyeTried = 0;

static void title_extra_backdrop(void)
{
    // Black underneath, always: it is what shows if the texture never arrives.
    memset(g_TimImageBuffer__bitmap, 0, 320 * 240 * 2);
    display_image(0, g_TimImageBuffer__bitmap, 320, 240);

    if (s_titleEye != MARNI_NULL_HANDLE || s_titleEyeTried) return;
    if (!IsGraphicsSystemReadyForOperation()) return;   // retry next visit
    s_titleEyeTried = 1;

    // 'REY2' + base w/h + iris w/h + the iris's home in the base, then both
    // layers as RGBA rows.
    const size_t head = 28;
    const size_t basePx = (size_t)TITLE_EYE_W * TITLE_EYE_H * 4;
    unsigned char* buf = (unsigned char*)malloc(head + basePx + 512 * 512 * 4);
    if (buf == NULL) return;

    const size_t read = LoadFile(GAME_DATA_ROOT "Data\\raideye.bin", buf, 0);
    if (read > head + basePx
        && buf[0] == 'R' && buf[1] == 'E' && buf[2] == 'Y' && buf[3] == '2') {
        const unsigned int bw = *(unsigned int*)(buf + 4);
        const unsigned int bh = *(unsigned int*)(buf + 8);
        const unsigned int iw = *(unsigned int*)(buf + 12);
        const unsigned int ih = *(unsigned int*)(buf + 16);
        const int ix = *(int*)(buf + 20);
        const int iy = *(int*)(buf + 24);
        const size_t want = head + basePx + (size_t)iw * ih * 4;

        if (bw == (unsigned int)TITLE_EYE_W && bh == (unsigned int)TITLE_EYE_H
            && read == want) {
            // bpp 32 is memcpy'd straight into an R8G8B8A8 texture, so the
            // file's byte order IS the texture's - which is what the baker
            // writes.
            MarniCreateTexture(TITLE_EYE_W, TITLE_EYE_H, 32, buf + head,
                               &s_titleEye);
            MarniCreateTexture((int)iw, (int)ih, 32, buf + head + basePx,
                               &s_titleIris);
            s_titleIrisW = (int)iw;
            s_titleIrisH = (int)ih;
            s_titleIrisX = ix;
            s_titleIrisY = iy;
        }
    }
    free(buf);
}

// Where the eye looks, and for how long. Saccades, not a drift: an eye holds
// still and then snaps, and the holding is most of it.
//
// Every offset is EVEN. The scanlines are baked into both layers, so an odd
// shift would put the iris's comb half a line out of phase with the eyeball's
// and the two would beat against each other along the join.
struct TitleEyeLook { short dx, dy, hold; };

static const TitleEyeLook s_titleEyeLook[] = {
    {   0,  0, 74 }, {  -8,  2, 56 }, {   6, -4, 82 }, {  10,  4, 48 },
    {  -4, -6, 66 }, {   2,  6, 92 }, {  -10,  0, 60 }, {   4, -2, 78 },
};

#define TITLE_EYE_LOOKS  ((int)(sizeof(s_titleEyeLook) / sizeof(s_titleEyeLook[0])))
#define TITLE_EYE_SNAP   6           // frames a saccade takes

static void title_extra_eye(int f)
{
    if (s_titleEye == MARNI_NULL_HANDLE) return;

    // The eye arrives WITH the word. Before that the screen is the bars on
    // black, and something watching from behind them would give the reveal
    // away.
    int a = (f - TITLE_EXTRA_F_WORD) * 7;
    if (a <= 0) return;
    if (a > 255) a = 255;
    const unsigned int tint = title_rgba(0xFFFFFFu, a);

    QueueTexturedSpriteTinted(-(float)TITLE_EYE_W * 0.5f,
                              -(float)TITLE_EYE_H * 0.5f,
                              (float)TITLE_EYE_W, (float)TITLE_EYE_H,
                              s_titleEye, TITLE_EYE_DEPTH, tint);

    if (s_titleIris == MARNI_NULL_HANDLE) return;

    // Walk the look table by the frame counter, easing over the snap.
    int t = f, i = 0;
    for (;;) {
        const int span = s_titleEyeLook[i].hold + TITLE_EYE_SNAP;
        if (t < span) break;
        t -= span;
        i = (i + 1) % TITLE_EYE_LOOKS;
    }
    const TitleEyeLook* from = &s_titleEyeLook[i];
    const TitleEyeLook* to   = &s_titleEyeLook[(i + 1) % TITLE_EYE_LOOKS];

    float dx = (float)from->dx, dy = (float)from->dy;
    if (t >= from->hold) {
        const float k = title_ease_out((float)(t - from->hold) / TITLE_EYE_SNAP);
        dx += ((float)to->dx - dx) * k;
        dy += ((float)to->dy - dy) * k;
    }
    // back to even pixels, whatever the ease produced
    const int idx = ((int)(dx < 0 ? dx - 0.5f : dx + 0.5f) / 2) * 2;
    const int idy = ((int)(dy < 0 ? dy - 0.5f : dy + 0.5f) / 2) * 2;

    QueueTexturedSpriteTinted(
        (float)(-TITLE_EYE_W / 2 + s_titleIrisX + idx),
        (float)(-TITLE_EYE_H / 2 + s_titleIrisY + idy),
        (float)s_titleIrisW, (float)s_titleIrisH,
        s_titleIris, TITLE_EYE_IRIS_D, tint);
}

// Two sounds, each its own file and its own bank - for the same reason the
// toast has one: a bank the game's own code never allocated is a bank it never
// throws away, and this screen can be opened at any point in the title loop.
//
//   raid.wav     the sting. One shot, its hit written to land on the frame
//                RAID rises (slot 0 - play once).
//   raidbgm.wav  the bed. An 8-bar loop under the whole screen (slot 1 - the
//                non-zero slot is what sets XAUDIO2_LOOP_INFINITE, which
//                repeats the WHOLE buffer; this build has no loop-region
//                mechanism, so the file itself has to be the loop).
//
// The bed sits well under the sting: it is what the screen sounds like, not
// what the screen says.
#define TITLE_EXTRA_SND_TRIM  (-100)
#define TITLE_EXTRA_BGM_TRIM  (-350)

// ---------------------------------------------------------------------------
// CUSTOM: the RESIDENT EVIL wordmark, drawn over the backdrop.
//
// It is the game's own lettering, cut out of the original title.pix by
// tools/build_title_bg.py and written as 'RLG1' + w + h + RGBA - the same
// shape as raideye.bin, and loaded the same way.  Nothing about it is
// redrawn; the baker only separates the pixels that were already there from
// the eye they were painted over.
//
// Drawing it as a sprite instead of leaving it in the picture is what lets
// the backdrop change at all.  It also means the wordmark keeps its own
// alpha, so it sits ON the room rather than being part of it.
//
// TITLE_LOGO_X/Y put it back where it was in title.pix, expressed in the
// centre-relative space the rest of this screen is laid out in: the crop
// started at 14,76 in a 320x240 picture whose centre is 160,120.
// ---------------------------------------------------------------------------
#define TITLE_LOGO_X  (-146.0f)
#define TITLE_LOGO_Y  (-44.0f)
#define TITLE_LOGO_D  1000u

static MarniHandle s_titleLogo = MARNI_NULL_HANDLE;
static int s_titleLogoW = 0, s_titleLogoH = 0;
static int s_titleLogoTried = 0;

static void title_load_logo(void)
{
    if (s_titleLogo != MARNI_NULL_HANDLE || s_titleLogoTried) return;
    if (!IsGraphicsSystemReadyForOperation()) return;   // retry next frame
    s_titleLogoTried = 1;

    const size_t head = 12;
    unsigned char* buf = (unsigned char*)malloc(head + 512 * 256 * 4);
    if (buf == NULL) return;

    const size_t read = LoadFile(GAME_DATA_ROOT "Data\\titlelogo.bin", buf, 0);
    if (read > head
        && buf[0] == 'R' && buf[1] == 'L' && buf[2] == 'G' && buf[3] == '1') {
        const unsigned int w = *(unsigned int*)(buf + 4);
        const unsigned int h = *(unsigned int*)(buf + 8);
        if (w > 0 && h > 0 && w <= 512 && h <= 256
            && read == head + (size_t)w * h * 4) {
            // bpp 32 goes straight into an R8G8B8A8 texture, so the file's
            // byte order IS the texture's - which is what the baker writes.
            MarniCreateTexture((int)w, (int)h, 32, buf + head, &s_titleLogo);
            s_titleLogoW = (int)w;
            s_titleLogoH = (int)h;
        }
    }
    free(buf);
}

// `a` is 0..255.  The screen's own fade runs as an overlay in front of
// everything, so this is only the wordmark's own entrance, not the fade.
static void title_draw_logo(int a)
{
    title_load_logo();
    if (s_titleLogo == MARNI_NULL_HANDLE) return;
    if (a <= 0) return;
    if (a > 255) a = 255;

    QueueTexturedSpriteTinted(TITLE_LOGO_X, TITLE_LOGO_Y,
                              (float)s_titleLogoW, (float)s_titleLogoH,
                              s_titleLogo, TITLE_LOGO_D,
                              title_rgba(0xFFFFFFu, a));
}

extern int g_SfxVolume;

static int s_titleExtraSnd = 0;
static int s_titleExtraBgm = 0;
static int s_titleExtraSndTries = 0;

static void title_extra_sound(int play)
{
    if (!play) {
        if (s_titleExtraSnd != 0) setSndStop(s_titleExtraSnd);
        if (s_titleExtraBgm != 0) setSndStop(s_titleExtraBgm);
        return;
    }

    // The device may not be up on the first frames; loadSndBankFromWav just
    // answers 0 then, so retry on a later visit rather than for ever.
    if (s_titleExtraSnd == 0 && s_titleExtraSndTries < 3) {
        s_titleExtraSndTries++;
        s_titleExtraSnd = loadSndBankFromWav(GAME_DATA_ROOT "Sound\\raid.wav");
        s_titleExtraBgm = loadSndBankFromWav(GAME_DATA_ROOT "Sound\\raidbgm.wav");
    }

    if (s_titleExtraBgm != 0) {
        set_volume(s_titleExtraBgm, g_SfxVolume + TITLE_EXTRA_BGM_TRIM);
        pan_set(s_titleExtraBgm, 0);
        playSnd(s_titleExtraBgm, 1);   // slot != 0: loop for ever
    }

    if (s_titleExtraSnd != 0) {
        set_volume(s_titleExtraSnd, g_SfxVolume + TITLE_EXTRA_SND_TRIM);
        pan_set(s_titleExtraSnd, 0);
        playSnd(s_titleExtraSnd, 0);
    }
}

// The voice gate:
//   0 nothing started
//   1 asked for it, waiting for the mixer to actually begin
//   2 heard it start, waiting for it to end
//   3 it never started - hold for the clip's own length instead
//   5 the phrase is over: fading the title art down to black
//   4 done - the menu has it from here
static int s_titleVoiceState = 0;
static int s_titleVoiceFrames = 0;

// The first version asked getSndStat six frames after play_sfx and treated
// "not playing" as "finished". Six frames is a few milliseconds - XAudio2 had
// not queued the buffer yet - so the menu opened almost instantly while the
// line played on underneath it, and because the pad was still down the menu
// took that as a confirm and flashed into the character select. Hence: never
// conclude anything from the status until the sound has been SEEN playing.
#define TITLE_VOICE_START_FRAMES 90    // patience for the mixer to begin
#define TITLE_VOICE_NOMINAL      110   // evil01 is 3.44 s at a 30 Hz tick
#define TITLE_VOICE_MAX_FRAMES   400   // hard backstop, never reached normally

// Frames to ignore the button for after the menu opens, so the press that
// started the phrase cannot also pick the first item.
static int s_titleMenuGuard = 0;

static const AchvRect* title_item_art(int item)
{
    switch (item) {
    case TITLE_ITEM_NEW:   return &g_achvMarkWordnew;
    case TITLE_ITEM_LOAD:  return &g_achvMarkWordload;
    case TITLE_ITEM_EXTRA: return &g_achvMarkWordextra;
    default:               return &g_achvMarkWordquit;
    }
}

// Game space -> backbuffer, the same transform AddTintSprite applies to
// everything else on this screen (screen offset first, then the render scale).
static void title_blit(const AchvRect* r, float x, float y, unsigned int color)
{
    float sx, sy;
    MarniGetRenderScale(&sx, &sy);
    const float gx = (x + (float)g_ScreenOffsetX) * sx;
    const float gy = (y + (float)g_ScreenOffsetY) * sy;
    UiAtlas_PushPixel(r, gx, gy, (float)r->w * sx, (float)r->h * sy, color, 520u);
}

// Like title_blit, but the quad's size is given rather than taken from the
// atlas rect - the flare art is small and drawn large on purpose.
static void title_blit_sized(const AchvRect* r, float x, float y,
                             float w, float h, unsigned int color,
                             unsigned int depth)
{
    float sx, sy;
    MarniGetRenderScale(&sx, &sy);
    UiAtlas_Push(r, (x + (float)g_ScreenOffsetX) * sx,
                 (y + (float)g_ScreenOffsetY) * sy, w * sx, h * sy, color, depth);
}

// A solid rectangle in the same space.
static void title_fill(float x, float y, float w, float h,
                       unsigned int color, unsigned int depth)
{
    float sx, sy;
    MarniGetRenderScale(&sx, &sy);
    UiAtlas_FillPushed((x + (float)g_ScreenOffsetX) * sx,
                       (y + (float)g_ScreenOffsetY) * sy,
                       w * sx, h * sy, color, depth);
}

// `dim` fades the whole menu with the screen, so it follows the original's
// fades instead of sitting at full brightness through them.
static void title_draw_menu(unsigned char dim)
{
    if (!UiAtlas_Ready()) return;

    const unsigned int lit  = 0xFF000000u | (0xFFFFFFu);
    const unsigned int idle = 0xFF000000u | (0x6E6E6Eu);

    for (int i = 0; i < TITLE_ITEM_COUNT; i++) {
        const AchvRect* art = title_item_art(i);
        unsigned int c = (i == s_titleMenuIndex) ? lit : idle;
        // scale the colour by the fade the caller asked for
        const unsigned int k = dim;
        unsigned int r = (((c >> 16) & 0xFF) * k) >> 7;
        unsigned int g = (((c >> 8) & 0xFF) * k) >> 7;
        unsigned int b = ((c & 0xFF) * k) >> 7;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        title_blit(art, (float)TITLE_MENU_X,
                   (float)(TITLE_MENU_Y + i * TITLE_MENU_STEP),
                   0xFF000000u | (r << 16) | (g << 8) | b);
    }

    // the copyright, centred as it was inside the original block
    {
        unsigned int v = (0x6Eu * (unsigned int)dim) >> 7;
        if (v > 255) v = 255;
        title_blit(&g_achvMarkWordcopy, (float)TITLE_COPY_X, (float)TITLE_COPY_Y,
                   0xFF000000u | (v << 16) | (v << 8) | v);
    }
}

// ---------------------------------------------------------------------------
// CUSTOM: the EXTRA screen's opening.
//
// Modelled on the mode-intro in GF2 Exilium: bars shoot in from the left at a
// stagger, clear to the right, a rule opens out of the centre, and the logo
// rises out of it under a light sweep. Everything here is drawn from the Space
// GUI atlas - the white block for the bars and the rule, the baked word for
// RAID - so it costs one texture and a handful of quads.
//
// The whole thing is a function of ONE counter, reset when the screen opens.
// Nothing keeps its own state, so the sequence cannot end up half-played: at
// any frame the screen is exactly what the frame number says it is.
// ---------------------------------------------------------------------------
static int s_titleExtraFrames = 0;

#define TITLE_EXTRA_LINE_Y    (-16) // the rule runs BEHIND the lettering
#define TITLE_EXTRA_RULE_Y    (16)

#define TITLE_EXTRA_D_GRAIN   508u
#define TITLE_EXTRA_D_SWEEP   516u
#define TITLE_EXTRA_D_WORD    520u
#define TITLE_EXTRA_D_LINE    528u
#define TITLE_EXTRA_D_FLARE   530u

#define TITLE_EXTRA_RED       0xC81418u

// Where the flare comes to rest: the middle of the LAST letter, in the screen's
// centre-relative space. The D inks columns 88..117 of the 118-wide baked word
// (measured off the art) and the word is drawn from -59.
//
// The light lands there and that is all it does. Repainting the letter itself
// in a warm colour was tried and taken out again: a letter that is lighter than
// the ones beside it is a letter that has been coloured differently, not a
// letter something is shining on. The glow does the work.
#define TITLE_EXTRA_D_C0   88
#define TITLE_EXTRA_D_C1   118
#define TITLE_EXTRA_D_CX   (TITLE_EXTRA_D_C0 + (TITLE_EXTRA_D_C1 - TITLE_EXTRA_D_C0) / 2 - 59)

// The opening used to put a readout of bars here, turning into a road that ran
// into the logo. It is gone: on this screen it was one idea too many, and what
// the screen wants before the logo is the dark and the riser under it, not
// something to watch. The rule opening out of nothing is the opening now.
//
// (tools/build_raid_bgm.py still marks the same frames; nothing about the sound
// depended on the bars.)

static void title_extra_rule(int f)
{
    const int lf = f - TITLE_EXTRA_F_LINE;
    if (lf < 0) return;

    float t = (float)lf / 11.0f;
    if (t > 1.0f) t = 1.0f;
    const float w = 316.0f * title_ease_out(t);

    // White while it is still opening, settling to the accent once it is out:
    // the flash is what makes it read as the thing that brings the logo in
    // rather than another bar.
    const unsigned int rgb = (lf < 12) ? 0xFFFFFFu : TITLE_EXTRA_RED;
    const float h = (lf < 8) ? 3.0f : 2.0f;

    // Once the word is rising the rule parts in the middle to make room for it.
    // Left whole it reads as a strike through the lettering - it passes behind
    // the strokes, but the gaps between them are exactly where a rule at this
    // height shows. Parting it turns the same line into two marks either side,
    // and the parting itself looks like the word pushing them apart.
    float gap = 0.0f;
    const int gf = f - TITLE_EXTRA_F_WORD;
    if (gf >= 0) {
        float t = (float)gf / 9.0f;
        if (t > 1.0f) t = 1.0f;
        gap = ((float)(g_achvMarkWordraid.w / 2) + 9.0f) * title_ease_out(t);  // over TITLE_EXTRA_GAP_F frames
    }

    const float end = w * 0.5f;
    if (end > gap) {
        title_fill(-end, (float)TITLE_EXTRA_LINE_Y, end - gap, h,
                   title_rgba(rgb, 255), TITLE_EXTRA_D_LINE);
        title_fill(gap, (float)TITLE_EXTRA_LINE_Y, end - gap, h,
                   title_rgba(rgb, 255), TITLE_EXTRA_D_LINE);
    }

    // A second, thinner rule under the word, drawn a few frames later - two
    // weights of the same line is what keeps it from looking like a divider.
    if (lf >= 9) {
        float t2 = (float)(lf - 9) / 12.0f;
        if (t2 > 1.0f) t2 = 1.0f;
        const float w2 = 188.0f * title_ease_out(t2);
        title_fill(-w2 * 0.5f, (float)TITLE_EXTRA_RULE_Y, w2, 1.0f,
                   title_rgba(0x5A5A64u, 255), TITLE_EXTRA_D_LINE);
    }
}

static void title_extra_word(int f)
{
    const int wf = f - TITLE_EXTRA_F_WORD;
    if (wf < 0) return;

    const AchvRect* art = &g_achvMarkWordraid;
    const float x = (float)(-(int)(art->w / 2));
    const float y = (float)TITLE_EXTRA_Y;

    title_blit(art, x, y, title_rgba(TITLE_EXTRA_RED, wf * 22));

    // The light sweep: a vertical slice of the SAME art, drawn white over the
    // top and walked across. Slicing the source rect rather than masking means
    // the highlight follows the letterforms exactly - it lights the strokes it
    // crosses and nothing between them.
    const int sf = wf - 2;
    if (sf < 0 || sf > 23) return;

    const int band = 16;
    const int span = art->w + band * 2;
    const int sx0  = (span * sf) / 23 - band;

    int c0 = sx0;
    int c1 = sx0 + band;
    if (c0 < 0) c0 = 0;
    if (c1 > art->w) c1 = art->w;
    if (c1 <= c0) return;

    AchvRect slice;
    slice.x = (short)(art->x + c0);
    slice.y = art->y;
    slice.w = (short)(c1 - c0);
    slice.h = art->h;

    float sx, sy;
    MarniGetRenderScale(&sx, &sy);
    UiAtlas_PushPixel(&slice,
                      (x + (float)c0 + (float)g_ScreenOffsetX) * sx,
                      (y + (float)g_ScreenOffsetY) * sy,
                      (float)slice.w * sx, (float)slice.h * sy,
                      title_rgba(0xFFFFFFu, 150), TITLE_EXTRA_D_SWEEP);
}

// The prompt, without the copyright.
//
// UpdateTitleTextSprite draws a 54-tall block, and the game bakes the two
// CAPCOM lines into it under the prompt - right for the title screen, which
// wants both, wrong here. Same page, same place, same colour; only the height
// is cut to the band the lettering sits in. Rows 5..15 carry it and nothing
// else until row 32, in BOTH variants of the page (t_start.tim for a pad,
// t_press.tim for the keyboard - they differ in wording, not in layout), so one
// band serves whichever one is loaded and the line keeps whatever it says.
#define TITLE_PROMPT_BAND_H 20

// The flare. A round falloff and a horizontal smear, both from the atlas, both
// white and faded by alpha - drawn BEHIND the lettering so it lights the word
// from the far side instead of washing it out. It rides in from the right,
// slows into place and settles at a level it keeps, which is what makes the
// finished screen feel lit rather than merely drawn.
static void title_extra_flare(int f)
{
    const int bf = f - TITLE_EXTRA_F_FLARE;
    if (bf < 0) return;

    // It travels along the rule, STOPS on the last letter, and after that it
    // only grows. It does not move again: the arrival is the event, and a light
    // that then wanders off takes the eye with it.
    //
    // The D's columns are 88..117 of the 118-wide art, so its middle is 44 px
    // right of centre - that is where the light stops and stays.
    float t = (float)bf / 40.0f;
    if (t > 1.0f) t = 1.0f;
    const float e = title_ease_out(t);

    const float cx = 150.0f - (150.0f - (float)TITLE_EXTRA_D_CX) * e;
    const float cy = (float)TITLE_EXTRA_LINE_Y + 2.0f;
    const float rad = 96.0f - 34.0f * e;

    int a = (bf < 21) ? (bf * 9) : (189 - (bf - 21) * 5);
    if (a < 96) a = 96;
    if (a > 189) a = 189;

    // The growth, once it has arrived. Not a scale about the centre: it opens
    // UP AND TO THE RIGHT, with the left edge and the bottom barely moving. An
    // even expansion would drag the bright middle along with it, which is the
    // thing this must not do - the hot point belongs on the letter, and only
    // the spill goes anywhere.
    float x0 = cx - rad, x1 = cx + rad;
    float y0 = cy - rad, y1 = cy + rad;
    float core = 15.0f;
    float warm = 1.0f;          // 1 = the yellow it arrives in, 0 = white
    int halo = a / 3;
    float grow = 0.0f;          // 0 until it has arrived, then 0..1

    const int lf = bf - 40;
    if (lf > 0) {
        float u = (float)lf / 70.0f;
        if (u > 1.0f) u = 1.0f;
        u = title_ease_out(u);
        x1   += 84.0f * u;     // right
        x0   -= 12.0f * u;
        y0   -= 52.0f * u;     // and up
        y1   +=  8.0f * u;
        core += 13.0f * u;
        // and it whitens. A light that is getting brighter loses its colour as
        // it goes: the hue survives at the level it arrived at, and burns out
        // of the spill as the spill gains.
        warm = 1.0f - 0.85f * u;
        // and it gains as it spreads. A radial falloff stretched over a bigger
        // quad puts less light through every pixel of it, so holding the alpha
        // while the quad grows makes the light FADE - which is the opposite of
        // growing. An earlier cut did exactly that and all but vanished.
        halo += (int)((float)(a - a / 3) * 0.72f * u);
        grow = u;
    }

    // The ghost arc. A lens throws one opposite whatever is blowing it out, so
    // this sits on the far side of the screen centre from the core, on the line
    // through it - that is what makes it read as an artefact of a lens rather
    // than as a shape someone drew. Drawn TWICE at slightly different sizes in
    // two tints, which is the chromatic fringe along its rim: real ghosts split
    // colour because the glass bends each wavelength by a different amount.
    if (grow > 0.0f) {
        const float gx = -cx * 1.45f;        // through the centre, and beyond
        const float gy = -cy * 1.45f + 42.0f;
        const float gr = 128.0f + 40.0f * grow;
        const int ga = (int)(96.0f * grow);

        title_blit_sized(&g_achvMarkFlarering, gx - gr * 1.04f, gy - gr * 1.04f,
                         gr * 2.08f, gr * 2.08f,
                         title_rgba(0x80C0FFu, (ga * 3) / 4), TITLE_EXTRA_D_FLARE);
        title_blit_sized(&g_achvMarkFlarering, gx - gr, gy - gr, gr * 2.0f, gr * 2.0f,
                         title_rgba(title_warm(0xFFD9A0u, warm), ga),
                         TITLE_EXTRA_D_FLARE);
    }

    // The spill: wide, and DIM. It is the thing the light throws, not the
    // light. Held down so that the core stays the brightest thing on screen -
    // a halo bright enough to compete turns the whole flare into a smudge,
    // which is what this used to be.
    title_blit_sized(&g_achvMarkGlow, x0, y0, x1 - x0, y1 - y0,
                     title_rgba(title_warm(0xFFCC66u, warm), halo),
                     TITLE_EXTRA_D_FLARE);

    // The core, drawn about cx rather than about the middle of the halo above -
    // that is the whole point of splitting them: the spill spreads, the hot
    // point does not move. Small and hot: in the reference this is a compact
    // point with a wide dim wash around it, and it is the compactness that
    // makes it read as a light rather than as fog.
    title_blit_sized(&g_achvMarkGlow, cx - core, cy - core,
                     core * 2.0f, core * 2.0f,
                     title_rgba(0xFFFFFFu, a), TITLE_EXTRA_D_FLARE);
    title_blit_sized(&g_achvMarkGlow, cx - core * 0.34f, cy - core * 0.34f,
                     core * 0.68f, core * 0.68f,
                     title_rgba(0xFFFFFFu, 255), TITLE_EXTRA_D_FLARE);

    // The smear sits on the rule's own line, which is what ties it to the rest
    // of the screen rather than floating over it. Centred on the letter too.
    title_blit_sized(&g_achvMarkStreak, cx - 156.0f, cy - 2.5f, 312.0f, 5.0f,
                     title_rgba(title_warm(0xFFE6B0u, warm), (a * 2) / 3),
                     TITLE_EXTRA_D_FLARE);
}

// Grain, over everything.
//
// One tile in the atlas and a 160x120 WINDOW of it, moved every frame: a whole
// screen of moving noise for a single quad, where a tile per frame would need
// a stack of them and per-pixel noise would need thousands. Stretched 2x and
// point-sampled, so a grain is a 2x2 block - film, not fizz. The window walks
// by a plain LCG off the frame counter: it only has to be unrepeatable to the
// eye, and the eye is easy.
static void title_extra_grain(int f)
{
    unsigned int r = (unsigned int)f * 1664525u + 1013904223u;
    r ^= r >> 16;

    const int ox = (int)(r % (unsigned int)(g_achvMarkGrain.w - 160));
    const int oy = (int)((r >> 8) % (unsigned int)(g_achvMarkGrain.h - 120));

    AchvRect win;
    win.x = (short)(g_achvMarkGrain.x + ox);
    win.y = (short)(g_achvMarkGrain.y + oy);
    win.w = 160;
    win.h = 120;

    float sx, sy;
    MarniGetRenderScale(&sx, &sy);
    UiAtlas_PushPixel(&win, 0.0f, 0.0f, 320.0f * sx, 240.0f * sy,
                      title_rgba(0xFFFFFFu, 4), TITLE_EXTRA_D_GRAIN);
}

static void title_draw_extra_prompt(unsigned char brightness)
{
    TextureDesc* td = &g_TextureDesc;

    // The original's own rule: 0x80 is the flat draw, anything else is the
    // modulated variant. Keeping it means the fade up reads the same way the
    // title screen's own text fade does.
    td->flags = (brightness == 0x80) ? 0x10000000 : 0x40000000;
    td->texU = 0;
    td->texV = 0;
    td->width = 256;
    td->height = TITLE_PROMPT_BAND_H;
    td->screenX = -130;
    td->screenY = g_titleTextPosTable[0].screenY + 38;
    td->texturePage = g_titleTexturePageData[0];
    td->colorMulR = brightness;
    td->colorMulG = brightness;
    td->colorMulB = brightness;
    td->clutX = 0;
    td->clutY = 0x1E0;
    td->pivotX = 0;
    td->pivotY = 0;

    display_texture(td, 2, 12, 1);
}

// RAID in red, centred on its own width, over the game's own prompt - the same
// lettering the title screen uses, still in texture slot 12, rather than a
// second font pretending to be it.
static void title_draw_extra(void)
{
    const int f = s_titleExtraFrames;

    // The prompt is the last thing in: it belongs to the finished screen, not
    // to the sequence that builds it.
    if (f >= TITLE_EXTRA_F_PROMPT) {
        int a = (f - TITLE_EXTRA_F_PROMPT) * 10;
        if (a > 0x80) a = 0x80;
        title_draw_extra_prompt((unsigned char)a);
    }

    if (!UiAtlas_Ready()) return;

    title_extra_eye(f);
    title_extra_rule(f);
    title_extra_flare(f);
    title_extra_word(f);
    title_extra_grain(f);
}

// Move the cursor. Returns 1 when it moved, so the caller can restart the
// attract-demo countdown exactly as the original did.
static int title_menu_move(unsigned short padPressed)
{
    if (padPressed & 0x1100) {          // up
        s_titleMenuIndex = (s_titleMenuIndex + TITLE_ITEM_COUNT - 1) % TITLE_ITEM_COUNT;
        return 1;
    }
    if (padPressed & 0x4000) {          // down
        s_titleMenuIndex = (s_titleMenuIndex + 1) % TITLE_ITEM_COUNT;
        return 1;
    }
    return 0;
}

// ============================================================================
// update_title_options (0x00430810)
// ============================================================================
void update_title_options(void)
{
	DWORD sidewinderPress = 0;
	DWORD sidewinderState = 0;
	if (g_bPadConnected) {
		sidewinderState = read_sidewinder_pad();
		sidewinderPress = sidewinderState & 0x10000 & ~g_PlayerPadHeldPrev;
	}
	g_PlayerPadHeldPrev = sidewinderState;

    // CUSTOM: the EXTRA screen. Its own mode, and its own two fades in and out
    // of black, arranged the same way as the title's handover: the swap always
    // happens under full black and the fade back is armed in the same frame, so
    // neither crossing shows a seam.
    if (g_titleMode == TITLE_MODE_EXTRA) {
        title_draw_extra();
        s_titleExtraFrames++;

        if (s_titleExtraPhase == 3) {
            if (g_fading_state > 0x7B80) {
                // CUSTOM: the RAID screen starts a run. It used to fade back
                // to the menu; now the press IS the start, so under full black
                // the title loop ends and title_state chains game_start.
                //
                // No backdrop swap and no fade back in here: the screen stays
                // black and game_start's own loading takes it from here, which
                // is what makes the press read as one move into the level
                // rather than a bounce off this screen.
                s_titleExtraPhase = 0;
                g_raidMode = 1;
                g_SelectedCharactedId = CHAR_JILL;
                g_titleSelectionId = TITLE_SEL_RAID;
                title_exit_loop();
            }
            return;
        }

        // The button that opened this screen is usually still down.
        if (s_titleExtraGuard > 0) {
            s_titleExtraGuard--;
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                s_titleExtraGuard = 2;
            }
            return;
        }

        if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
            // Cut the sting with the screen. Left running it would play on over
            // the menu, where it means nothing.
            title_extra_sound(0);
            s_titleExtraPhase = 3;
            g_fading_state = 0;
            g_fade_type_id = 2;
            g_fading_counter = 0x400;
            fade_update();
        }
        return;
    }

    if (g_titleMode != 0) {
        if (g_titleMode != 1) return;

        switch (g_titleOptionsFading) {
        case 0:
            g_titleOptionsFading = 1;
            g_fade_type_id = 2;
            g_fading_counter = 0xFC00;
            g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
            fade_update();
            return;

        case 1:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            title_draw_menu(128);
            return;

	case 2:
		// CUSTOM: the skinned menu replaces the original's two-item sprite.
		title_draw_menu(128);

		// On the way to the EXTRA screen: fade the menu out, then hand over
		// under full black. Input is dead for the duration - the pick has
		// already been made.
		if (s_titleExtraPhase == 1) {
			if (g_fading_state > 0x7B80) {
				title_extra_backdrop();
				title_extra_sound(1);
				s_titleExtraPhase = 2;
				s_titleExtraGuard = 8;
				s_titleExtraFrames = 0;   // the sequence plays from the top every time
				g_titleMode = TITLE_MODE_EXTRA;
				g_fading_state = 0;
				g_fade_type_id = 2;
				g_fading_counter = 0xFC00;
				fade_update();
				g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
			}
			break;
		}

		// The button that started the title phrase is often still down when
		// the menu appears; without this it would be read as picking the first
		// item the moment the list showed up.
		if (s_titleMenuGuard > 0) {
			s_titleMenuGuard--;
			if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
				s_titleMenuGuard = 2;    // still held: keep waiting
			}
			g_titleDemoTime--;
			break;
		}

		if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
			if (s_titleMenuIndex == TITLE_ITEM_EXTRA) {
				play_sfx(SFX_BANKS, 1); // null sfx
				s_titleExtraPhase = 1;
				g_fading_state = 0;
				g_fade_type_id = 2;
				g_fading_counter = 0x400;
				fade_update();
				break;
			}
			if (s_titleMenuIndex == TITLE_ITEM_QUIT) {
				// The same close the window's own X button takes: WM_DESTROY
				// runs the settings save and the sound-bank teardown before
				// the message loop returns.
				plat_window_destroy(g_hWnd);
				return;
			}
			// NEW GAME and LOAD GAME keep the original's exit codes -
			// title_state switches on g_titleSelectionId, 1 = character
			// select, 2 = load.
			g_titleSelectionId = (s_titleMenuIndex == TITLE_ITEM_NEW) ? 1 : 2;
			play_sfx(SFX_BANKS, 1); // null sfx
			g_titleOptionsFading = 6;
			g_fade_type_id = 1;
			g_fading_counter = 0x7F00;
			fade_update();
			g_bGameActive = 2;
			return;
		}

		if (title_menu_move((unsigned short)g_PlayerPadPressed)) {
			g_titleDemoTime = 0x708;
		}

            // (the original's demo_reset label is gone with the goto that used
            // it: the two-item wrap-around it existed for is now handled by
            // title_menu_move.)
            g_titleDemoTime--;
            if (g_titleDemoTime != 0) break;

            g_titleOptionsFading = 3;
            g_fade_type_id = 2;
            g_fading_counter = 0x400;
            fade_update();
            return;

        case 3:
            if (g_fading_state < 0) {
                g_titleSelectionId = 0;
                title_exit_loop();
                return;
            }
            title_draw_menu(128);
            if ((g_PlayerPadPressed & 0xeff) == 0) {
                return;
            }
            g_titleOptionsFading = 0;
            g_fading_counter = 0xF000;
            return;

        case 4:
            if (g_fading_state < 0) {
                title_exit_loop();
                g_main_state_flags &= MSF_SCREEN_MODE_MASK;
                return;
            }
            title_draw_menu(128);

        case 6:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 7;
                g_fade_type_id = 1;
                g_fading_counter = 0xC000;
                fade_update();
                title_draw_menu(128);
                return;
            }

        case 8:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 9;
                g_fade_type_id = 1;
                g_fading_counter = 0xF800;
                fade_update();
                title_draw_menu(128);
                return;
            }
            break;

        case 7:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 8;
                g_fade_type_id = 1;
                g_fading_counter = 0x8000;
                fade_update();
                title_draw_menu(0x80);
                return;
            }

        case 9:
            if (g_fading_state < 0) {
                g_titleOptionsFading = 4;
                g_fade_type_id = 2;
                g_fading_counter = 0x270;
                fade_update();
                title_draw_menu(0x80);
                return;
            }

        default:
            break;
        }

        title_draw_menu(0x80);
        return;
    }

    // CUSTOM: the wordmark.  It used to be part of the backdrop and so was
    // simply there; now it is a sprite and has to be asked for, every frame
    // this screen is up.  This is the title-art phase only - the menu swaps
    // the picture underneath for SEL_BACK and the name goes with it, exactly
    // as it did before.
    //
    // Unconditional, and at full alpha: the screen's fades are an overlay in
    // front of every sprite, so the wordmark comes up and goes down with the
    // room behind it without needing to be told.
    title_draw_logo(255);

    switch (g_titleOptionsFading) {
        case 0:
            g_titleOptionsFading = 1;
            g_titleDemoTime = 0x80;
            goto option_selected;
        case 1:
    option_selected:
            g_titleDemoTime -= 4;
            UpdateTitleTextSprite(-0x80 - (char)g_titleDemoTime, 0);
            if (g_titleDemoTime == 0) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                g_titleOptionsFading = 2;
                g_titleDemoTime = 0x708;
            }
            break;

        case 2:
            // The prompt is only up while the screen is still waiting for a
            // button. Once one is pressed the phrase takes over, so PRESS ANY
            // BUTTON goes out with the flash below rather than sitting on
            // screen through a line that is already answering it.
            if (s_titleVoiceState == 0 || s_titleVoiceState == 4) {
                UpdateTitleTextSprite(0x80, 0);
            }

            // CUSTOM: the game says its own name here. The press starts the
            // line, the screen holds on PRESS ANY BUTTON while it plays, and
            // only when it has finished does the menu open - so the two never
            // talk over each other. Further presses are ignored on purpose:
            // the phrase is the opening beat, not a thing to click past.
            // The handover. The menu is a different picture on a different
            // backdrop, so cutting to it the instant the phrase stops is a
            // jump however the menu then fades up. Instead the title art fades
            // out first, the swap happens under full black, and the fade back
            // in is armed in the SAME frame - so the black never lifts between
            // the two and the whole thing reads as one move.
            if (s_titleVoiceState == 5) {
                if (g_fading_state > 0x7B80) {
                    s_titleMenuIndex = TITLE_ITEM_NEW;
                    s_titleMenuGuard = 8;
                    title_menu_backdrop();
                    s_titleVoiceState = 4;
                    g_titleMode = 1;
                    // Step 1, not 0: step 0 would arm the fade a frame later,
                    // and that frame would show the new backdrop at full
                    // brightness before the black arrived - a flash of the
                    // menu before its own entrance. Arm it here instead and
                    // let step 1 do what it exists for: hold until it clears.
                    g_titleOptionsFading = 1;
                    g_fading_state = 0;
                    g_fade_type_id = 2;
                    g_fading_counter = 0xFC00;
                    fade_update();
                    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
                }
                break;
            }

            if (s_titleVoiceState != 0 && s_titleVoiceState != 4) {
                const int handle = g_SfxBanks[SFX_TITLE_EVIL01 * 2];
                int done = 0;
                s_titleVoiceFrames++;

                if (s_titleVoiceState == 1) {
                    if (handle != 0 && getSndStat(handle) == 1) {
                        s_titleVoiceState = 2;          // it is really playing
                        s_titleVoiceFrames = 0;
                    } else if (s_titleVoiceFrames >= TITLE_VOICE_START_FRAMES) {
                        // No device, or the bank never loaded. Hold anyway: the
                        // beat is the same length whether it is heard or not.
                        s_titleVoiceState = 3;
                        s_titleVoiceFrames = 0;
                    }
                } else if (s_titleVoiceState == 2) {
                    done = (getSndStat(handle) != 1)
                           || (s_titleVoiceFrames > TITLE_VOICE_MAX_FRAMES);
                } else {
                    done = (s_titleVoiceFrames >= TITLE_VOICE_NOMINAL);
                }

                if (done) {
                    // Start the fade out. 0x400 a frame is the same rate the
                    // title itself uses to leave when it times out, and it is
                    // matched by the fade in on the other side, so the two
                    // halves of the handover are the same length.
                    s_titleVoiceState = 5;
                    g_fading_state = 0;
                    g_fade_type_id = 2;
                    g_fading_counter = 0x400;
                    fade_update();
                }
                break;
            }

            g_titleDemoTime--;
            if (g_titleDemoTime == 0) {
                g_titleOptionsFading = 3;
                g_fade_type_id = 2;
                g_fading_counter = 0x400;
                fade_update();
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                play_sfx(SFX_BANKS, SFX_TITLE_EVIL01);
                s_titleVoiceState = 1;
                s_titleVoiceFrames = 0;

                // The press gets the same white flash a menu pick gets, then
                // the picture comes back out of it.
                //
                // g_fade_type_id picks the COLOUR, and it is not a brightness:
                // draw_rect reads it as a variant, 1 = the white overlay and
                // 2 = the black one. This was set to 2, which is why the press
                // produced no white - it was drawing the black veil at an
                // alpha that started at full and fell away, i.e. a fade-in
                // from black over a screen that was already there.
                //
                // Armed the way the game arms a fade-IN: full level on the
                // press frame, minus 0x400 every frame after, so white covers
                // the screen at once and then thins out over about a second
                // while the phrase plays. fade_update only arms from a level
                // of 0 or less, and what the level is here is left over from
                // the title's own fade-in, so it is put there rather than
                // assumed.
                g_fading_state = 0;
                g_fade_type_id = 1;
                g_fading_counter = 0xFC00;
                fade_update();
            }
            break;

        case 3:
            if (g_fading_state > 0x7B80) {
                g_fading_state = 0x7FFF;
                g_fading_counter = 0;
                g_titleSelectionId = 0;
                g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
                title_exit_loop();
            }
            if ((g_PlayerPadPressed & 0xeff) || sidewinderPress) {
                g_titleOptionsFading = 2;
                g_fading_state = -1;
                g_titleDemoTime = 0x708;
            }
            UpdateTitleTextSprite(0x80, 0);
            break;

        case 4:
            UpdateTitleTextSprite(0x80, 0);
            if (g_fading_state > 0x7B80) {
                g_titleMode = 1;
                g_titleOptionsFading = 0;
                g_fading_state = 0x7FFF;
                g_fading_counter = 0;
                g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
            }
            break;
    }
}

// ============================================================================
// title_state (0x00430470)
// Title screen state: displays title, waits for player selection,
// then chains to game_start, logos_state, or characterSelectionScreen.
// ============================================================================
void title_state(void)
{
    int i;

	g_main_state_flags &= ~MSF_INTENSITY_RAMP;
	g_PlayerPadHeldPrev = 0;
	g_PlayerPadHeld = 0;
	g_RawPadHeld = 0;
	g_PlayerPadPressed = 0;
	g_playingGameFlag = 0;
	g_menu_choice_id = 0;

	// CUSTOM: every visit to the title starts outside RAID. Clearing it here
	// rather than at the end of a run means it cannot survive one however the
	// run ended - death, quit, or the ending.
	g_raidMode = 0;
	g_coopActive = 0;   // CUSTOM: co-op is RAID-only
	CoopNet_Stop();     // CUSTOM: and so is the socket

    setMenuScreenOffset(320, 240, 0, 0, 0);
    CenterScreenOrigin();
    clear_textures();
    set_scene_render_param(0xc0);
    sounds_reset();

    g_loadDataDestPointer = g_DataBuffer;
    LoadSoundBank(BANK_TITLE, g_DataBuffer);

    g_fading_state = -1;
    g_titleLoopFlag = 0;
    g_SpecialRoomLightState = 0xFFFF;
    g_titleMode = 0;
    g_titleOptionsFading = 0;

    init_title_screen();

    // empty_00497c10(0);
    // empty_0040abb0((void*)0, 0, 0, 0);

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    // 0x0047b950 call site: the original calls 0x00483510 here, a stub that
    // just returns 0 - call dropped

    setMenuScreenOffset(320, 240, 0, 0, 1);
    Task_sleep(1);

    if (g_fmvPlayCount < 1) {
        g_selectedFmvId = 0;
        g_fmvDataPointer = g_loadDataDestPointer;
        g_fmvPlayCount = 0x10;
        g_main_state_flags = g_main_state_flags | MSF_FMV_REQUEST;
        Task_sleep(1);
    }

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_REBUILD;
    Task_sleep(1);

    do {
        update_title_options();
        Task_sleep(1);
    } while (g_titleLoopFlag != 0);

    // legacy gpu wait
    if (g_GPU_VENDOR_ID == 1) {
        for (i = 180; i != 0; i--) {
            Task_sleep(1);
        }
    }

    cleanup_texture_slot(12);

    switch (g_titleSelectionId) {
    case 0:
        nullsub_0047eb80();
        g_main_state_flags2 |= MSF2_ATTRACT_DEMO;
        Task_chain((void*)game_start);
        Task_chain((void*)logos_state);
        return;

    // CUSTOM: RAID. Straight to gameplay - the character is already chosen
    // (the RAID screen set it) and there is no save to restore, so neither the
    // character select of case 1 nor the LoadSaveGameState of cases 2/3
    // applies. InitializeGame reads g_raidMode and takes it from there.
    case TITLE_SEL_RAID:
        nullsub_0047eb80();
        // Belt and braces: MSF2_ATTRACT_DEMO decides which of InitializeGame's
        // two player-setup branches runs, and a run must take the real one.
        // game_start already drops the bit on its way out of a demo, so this
        // only matters if that ever stops being true.
        g_main_state_flags2 &= ~MSF2_ATTRACT_DEMO;
        Task_chain((void*)game_start);
        return;

    case 1:
        nullsub_0047eb80();
        Task_chain((void*)characterSelectionScreen);

    case 2:
    case 3:
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
        LoadSaveGameState(1, 0x80180000, 0, 1, 0);
        g_loadSaveStateFlag = 0;
        Game_timer = g_gameTimerSnapshot;
        g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
        nullsub_0047eb80();
        Task_chain((void*)game_start);

    default:
        return;
    }
}

// nullsub_0047eb80 - empty no-op in the original PC build.
// likely a PS1 version function stripped during the PC port.
void nullsub_0047eb80(void) { }
