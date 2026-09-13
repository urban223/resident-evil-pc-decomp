// UiSkin.cpp - CUSTOM (port-only): the Space GUI status screen.
//
// Everything here is queued through UiAtlas_Push, i.e. into the renderer's
// pending-sprite list at an explicit depth, NOT drawn immediately. That is the
// whole trick that lets a port-added skin live under the game's own art: the
// engine's item icons are command sprites at depth*16 + 500 (644-676), so
//
//     depth 1000  the scrim that dims the frozen room
//     depth  700  plates, cells, the info card          <- behind the icons
//     depth  690  text on those plates
//     depth  600  cursor brackets, markers, glints      <- in front of them
//
// Coordinates are the game's 320x240 logical space and are converted with
// MarniGetRenderScale - the same transform AddTintSprite uses - so the skin
// lands exactly where the engine puts the icons it still draws itself.
#include "UiSkin.h"
#include "UiAtlas.h"
#include "../marni/MarniSystem.h"

#include <stdio.h>
#include <string.h>

// --- depths -----------------------------------------------------------------
#define D_SCRIM   1000u
#define D_PLATE    700u
#define D_TEXT     690u
#define D_FRONT    600u
#define D_MENU     560u   // the item action menu, over the icons and the cards
#define D_MENU_TX  550u
// Behind the message glyphs (AddTintSprite: fade*16 + 0x1C2, so 450 or 482)
// and in front of everything the skin queues at 500 and above.
#define D_MSG_BACK 490u

// --- palette (the toast's, so the whole port-added UI is one family) --------
#define C_GLASS   0xCD0A1E22u   // translucent plate
#define C_CARD    0xCD081A1Eu   // the info card, a touch denser
#define C_LINE    0xAA5ADCD2u   // ordinary edge
#define C_EDGE    0xFF78F0E2u   // lit edge / cursor
#define C_LABEL   0xFF4E9694u   // small labels
#define C_TEXT    0xFFE4FFFCu
#define C_BODY    0xFFCEEAE8u
#define C_DIM     0xA03C8282u   // filler
#define C_AMBER   0xE6FFB03Cu
#define C_ORANGE  0xDCFF9650u
#define C_LIME    0xFFC6FF4Au
#define C_SCRIM   0xCD040A0Eu

// --- layout, from claude/status-screen-skin-spec.md --------------------------
#define CELL_W 44
#define CELL_H 34
#define CELL_GX 47              // column pitch
#define CELL_GY 37              // row pitch
#define GRID_X 127
#define GRID_Y 23
#define GRID_COLS 4
#define GRID_ROWS 4

#define CARD_X0 124
#define CARD_X1 313
#define RULE_Y  173
#define NAME_Y  178

#define EQUIP_X 8
#define EQUIP_Y 70
#define OTHER_X 8
#define OTHER_Y 118
#define PORTRAIT_X 8
#define PORTRAIT_Y 22

#define ECG_X 8
#define ECG_Y 164
#define ECG_W 94
#define ECG_H 44

// Where the game's own EKG trace is stretched to. Inside the condition block,
// above the status word, which keeps its line at the bottom edge. The band
// starts four pixels below the block's frame so the tallest beat - poison
// reaches -15 from the baseline - has air above it instead of running into the
// border.
//
// The block grew upward into the gap under the OTHER slot, which ends at 152.
// Its label sits at ECG_Y - 8 like every other label in this column, so 164
// leaves the same few pixels of air above the word that EQUIPPED and OTHER
// have - any higher and CONDITION crowds the slot above it.
//
// The band is never made SHORTER than the source window (23 px, see
// SKIN_EKG_SRC_Y0..Y1 in MainMenu.cpp): shrinking collapses neighbouring
// samples of the wave onto one row, which is what flattened the trace into
// steps when the band was 15. Stretching is harmless - a segment is drawn
// between its endpoints, not row by row - so the band takes whatever height
// the block can spare above the status word.
#define ECG_EKG_X 10
#define ECG_EKG_Y 168
#define ECG_EKG_W 90
#define ECG_EKG_H 26

// The 0xa2 baseline's place in that band - (0xa2 - 0x91) * H / 23 - i.e. where
// a flat trace sits, which is the one grid line that means anything.
#define ECG_EKG_BASE 19

// Text sizes in game pixels; the atlas fonts are baked much larger, so the
// draw scale is (want / baked line height).
#define T_NAME  11
#define T_SMALL  7

static int s_enabled = 1;
static int s_roomVisible = 0;

int  UiSkin_Enabled(void)      { return s_enabled; }
int  UiSkin_RoomVisible(void)  { return s_enabled && s_roomVisible; }
void UiSkin_SetRoomVisible(int on) { s_roomVisible = on ? 1 : 0; }

void UiSkin_EkgRect(short* x, short* y, short* w, short* h)
{
    if (x) *x = ECG_EKG_X;
    if (y) *y = ECG_EKG_Y;
    if (w) *w = ECG_EKG_W;
    if (h) *h = ECG_EKG_H;
}
void UiSkin_SetEnabled(int on) { s_enabled = on ? 1 : 0; }

// ---------------------------------------------------------------------------
// Game space -> backbuffer. Recomputed once per draw: the window can be
// resized between frames.
// ---------------------------------------------------------------------------
static float s_sx = 1.0f, s_sy = 1.0f;

static void skin_begin(void)
{
    MarniGetRenderScale(&s_sx, &s_sy);
}

static void fill(float x, float y, float w, float h, unsigned int c, unsigned int depth)
{
    UiAtlas_FillPushed(x * s_sx, y * s_sy, w * s_sx, h * s_sy, c, depth);
}

// A one-pixel-thick outline, four quads. Thickness is in game pixels so it
// stays a hairline at every window size.
static void frame_rect(float x, float y, float w, float h, unsigned int c,
                       unsigned int depth)
{
    fill(x, y, w, 1, c, depth);
    fill(x, y + h - 1, w, 1, c, depth);
    fill(x, y, 1, h, c, depth);
    fill(x + w - 1, y, 1, h, c, depth);
}

static void mark(const AchvRect* r, float x, float y, float w, float h,
                 unsigned int c, unsigned int depth)
{
    UiAtlas_Push(r, x * s_sx, y * s_sy, w * s_sx, h * s_sy, c, depth);
}

// For the hand-plotted marks - the empty-slot cross and the action icons.
// They are pixel art, so they want nearest-neighbour; filtered up to forty
// screen pixels a ten-texel icon turns to mush and loses its outer column.
static void pixel_mark(const AchvRect* r, float x, float y, float w, float h,
                       unsigned int c, unsigned int depth)
{
    UiAtlas_PushPixel(r, x * s_sx, y * s_sy, w * s_sx, h * s_sy, c, depth);
}

static float text_k(int font)
{
    const float line = (font == UI_FONT_TITLE) ? (float)ACHV_TITLE_LINE
                                               : (float)ACHV_BODY_LINE;
    const float want = (font == UI_FONT_TITLE) ? (float)T_NAME : (float)T_SMALL;
    return (want / line);
}

static void label(int font, const char* s, float x, float y, unsigned int c)
{
    const float k = text_k(font);
    UiAtlas_TextPushed(font, s, x * s_sx, y * s_sy, k * s_sy, c, D_TEXT);
}

static float label_w(int font, const char* s)
{
    return UiAtlas_TextWidth(font, s, text_k(font));
}

static void label_right(int font, const char* s, float right, float y, unsigned int c)
{
    label(font, s, right - label_w(font, s), y, c);
}

// Break `s` into lines that fit `width` game pixels, splitting on spaces.
// Returns how many lines were written. A word longer than the line is left to
// overflow rather than being cut - there is no such word in this game's text,
// and a visible overflow is easier to notice than a silent truncation.
#define WRAP_MAX_CHARS 64

static int wrap_text(int font, const char* s, float width,
                     char out[][WRAP_MAX_CHARS], int maxLines)
{
    if (s == NULL || maxLines <= 0) return 0;

    int line = 0, n = 0, lastSpace = -1;
    out[0][0] = '\0';

    for (; *s != '\0'; s++) {
        if (n >= WRAP_MAX_CHARS - 1) break;
        out[line][n] = *s;
        out[line][n + 1] = '\0';
        if (*s == ' ') lastSpace = n;
        n++;

        if (label_w(font, out[line]) <= width) continue;
        if (line + 1 >= maxLines) { out[line][--n] = '\0'; break; }

        // over the edge: fall back to the last space and start a new line
        int carry = 0;
        char tail[WRAP_MAX_CHARS];
        if (lastSpace >= 0) {
            for (int i = lastSpace + 1; i < n; i++) tail[carry++] = out[line][i];
            out[line][lastSpace] = '\0';
        } else {
            tail[carry++] = out[line][n - 1];
            out[line][n - 1] = '\0';
        }
        tail[carry] = '\0';

        line++;
        n = 0;
        lastSpace = -1;
        for (int i = 0; i < carry; i++) {
            out[line][n] = tail[i];
            if (tail[i] == ' ') lastSpace = n;
            n++;
        }
        out[line][n] = '\0';
    }
    return line + 1;
}


// ---------------------------------------------------------------------------
// Where the engine should draw what it still owns
// ---------------------------------------------------------------------------
void UiSkin_SlotRect(int slot, short* x, short* y)
{
    if (slot < 0) slot = 0;
    // The engine's icons are 40x30 and the cell is 44x34, so they sit 2 in.
    if (x) *x = (short)(GRID_X + (slot % GRID_COLS) * CELL_GX + 2);
    if (y) *y = (short)(GRID_Y + (slot / GRID_COLS) * CELL_GY + 2);
}

void UiSkin_EquippedRect(short* x, short* y)
{
    if (x) *x = (short)(EQUIP_X + 2);
    if (y) *y = (short)(EQUIP_Y + 2);
}

void UiSkin_OtherRect(short* x, short* y)
{
    if (x) *x = (short)(OTHER_X + 2);
    if (y) *y = (short)(OTHER_Y + 2);
}

void UiSkin_PortraitRect(short* x, short* y)
{
    if (x) *x = (short)(PORTRAIT_X + 4);
    if (y) *y = (short)(PORTRAIT_Y + 2);
}

// ---------------------------------------------------------------------------
// Pieces
// ---------------------------------------------------------------------------
static void cell(float x, float y, float w, float h, int filled, int selected)
{
    fill(x, y, w, h, C_GLASS, D_PLATE);
    frame_rect(x, y, w, h, selected ? C_EDGE : C_LINE, D_PLATE);
    if (!filled) {
        // the empty-slot cross, one baked quad. Stepping a diagonal out of
        // fills costs dozens of sprites per cell and the pending list is not
        // deep enough for ten cells' worth.
        const float s = h * 0.42f;
        // D_PLATE - 1: in front of the plate it sits on, still behind the
        // engine's item icons (644-676). It shared D_PLATE at first and lost
        // the coin flip inside that band - which is what turned up the
        // unstable pending-sprite sort in Rendering.cpp.
        pixel_mark(&g_achvMarkCross, x + (w - s) * 0.5f, y + (h - s) * 0.5f,
                   s, s, 0x9C6EAAA8u, D_PLATE - 1u);
    }
}

// A short run of light travelling a border, head bright and tail fading. The
// border is quads already, so the glint is just more of them, drawn in front.
static void glint(float x, float y, float w, float h, int frame, float speed,
                  int len, unsigned int color)
{
    const float per = 2.0f * (w + h);
    float pos = (float)frame * speed;
    for (int i = 0; i < len; i++) {
        const float t = 1.0f - (float)i / (float)len;
        const unsigned int a = (unsigned int)(230.0f * t * t);
        if (a == 0) continue;
        float p = pos - (float)i;
        p = p - per * (float)((int)(p / per));
        if (p < 0.0f) p += per;

        float px, py;
        if (p < w)                 { px = x + p;               py = y; }
        else if (p < w + h)        { px = x + w;               py = y + (p - w); }
        else if (p < 2.0f * w + h) { px = x + w - (p - w - h); py = y + h; }
        else                       { px = x;                   py = y + h - (p - 2.0f * w - h); }

        fill(px - 0.5f, py - 0.5f, 2.0f, 2.0f,
             (a << 24) | (color & 0x00FFFFFFu), D_FRONT);
    }
}

static void pip_row(float x, float y, int n, int lit, const AchvRect* shape,
                    unsigned int on, unsigned int off)
{
    for (int i = 0; i < n; i++) {
        mark(shape, x + i * 7.0f, y, 5.0f, 5.0f, (i < lit) ? on : off, D_TEXT);
    }
}

// The filler. Every one of these lives in dead space - the band above the
// panels, the gutter between the columns, the strip under the description -
// and never where a label or a block already is.
static void filler(int frame)
{
    // signal bars, each breathing on its own period
    static const float base[6] = { 14, 22, 16, 26, 19, 13 };
    static const float amp[6]  = {  7, 11,  6, 13,  9,  5 };
    static const int   per[6]  = { 57, 89, 43, 127, 71, 49 };
    for (int i = 0; i < 6; i++) {
        const int p = (frame + i * 11) % per[i];
        const float half = (float)per[i] * 0.5f;
        const float tri = (p < half) ? (p / half) : (2.0f - p / half);
        fill(8, 3 + i * 1.6f, base[i] + amp[i] * tri, 1,
             (i & 1) ? 0xA55AD2CDu : 0x695AD2CDu, D_TEXT);
    }
    label(UI_FONT_BODY, "SYS 021  LINK OK", 48, 2, C_DIM);

    // a segment row with a lit pair cycling through it
    for (int i = 0; i < 10; i++) {
        const int lit = ((frame / 6 + i) % 10) < 2;
        fill(120 + i * 5, 3, 3, 3, lit ? 0xDC8CF0E6u : 0x5A468A88u, D_TEXT);
    }

    // The gutter ladder, running the full height of the column it sits in:
    // from the rule under the tabs down to the bottom of the info card (226),
    // so it reads as one measuring edge rather than a strip that gives up
    // two thirds of the way down.
    // Counted from the BOTTOM: the labelled ticks fall every fifth step
    // measured back from the last one, so the run ends on a numbered tick at
    // the card's bottom edge instead of trailing off unlabelled. The top tick
    // is the short one, which is the right way round for a scale that is being
    // read downwards.
    for (int i = 0; i < 30; i++) {
        const float y = 24 + i * 7.0f;
        const int lng = ((29 - i) % 5) == 0;
        fill(106, y, lng ? 9.0f : 4.0f, 1, lng ? 0x785AC8C4u : 0x465AC8C4u, D_TEXT);
        if (lng) {
            char num[8];
            snprintf(num, sizeof(num), "%03d", 21 + i);
            label(UI_FONT_BODY, num, 116, y - 3, C_DIM);
        }
    }
    // The connector marks pair with a labelled tick, as they did before - the
    // first, the fourth and the last of them, so the bottom of the column is
    // as furnished as the top.
    static const float kConnY[3] = { 53.0f, 158.0f, 228.0f };
    for (int i = 0; i < 3; i++) {
        const float y = kConnY[i];
        fill(104, y, 22, 1, 0x5A46AAA8u, D_TEXT);
        fill(112, y - 2, 1, 5, 0x9678DCD8u, D_TEXT);
        fill(116, y - 2, 1, 5, 0x9678DCD8u, D_TEXT);
    }

    // A service line that keeps changing - the readout equivalent of a blinking
    // light. Each entry holds for a few seconds and arrives a character at a
    // time, so the eye catches movement down there without anything moving.
    //
    // One line, not two: 240 is the last row of the screen and a second line
    // under this one was running off the bottom edge, half drawn.
    {
        static const char* const kLog[6] = {
            "MANSION 1F / SECTOR B",
            "CAM 04 LOCKED",
            "BUS SYNC 88%",
            "MEM 1F / 2F OK",
            "DOOR SEAL HOLD",
            "PWR RAIL NOMINAL",
        };
        const int slot = (frame / 110) % 6;
        const char* src = kLog[slot];
        const int phase = frame % 110;
        int n = (int)strlen(src);
        if (phase < n) n = phase;            // type it in
        char buf[24];
        if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
        for (int i = 0; i < n; i++) buf[i] = src[i];
        buf[n] = '\0';
        label(UI_FONT_BODY, buf, 8, 232, C_DIM);
        // the cursor, while the line is still arriving
        if (phase <= (int)strlen(src)) {
            fill(8 + label_w(UI_FONT_BODY, buf) + 1, 232, 3, 5,
                 ((frame / 4) & 1) ? 0xB478DCD8u : 0x40468C8Au, D_TEXT);
        }
    }

    // The screen edges. Both margins are 6 px of nothing - left of the CHARACTER
    // column and right of the info card - and a rail there gives the screen a
    // frame that is always moving without ever crossing anything that matters.
    for (int side = 0; side < 2; side++) {
        const float rx = side ? 314.0f : 1.0f;
        const float top = 14.0f, bot = 228.0f, span = bot - top;
        fill(rx + 2, top, 1, span, 0x3C468C8Au, D_TEXT);
        // ticks drifting, each side the opposite way
        for (int i = 0; i < 16; i++) {
            float p = (float)(i * 14) + (float)frame * (side ? -0.35f : 0.35f);
            p = p - span * (float)((int)(p / span));
            if (p < 0.0f) p += span;
            const int lng = (i % 4) == 0;
            fill(rx + (lng ? 0.0f : 1.0f), top + p, lng ? 5.0f : 3.0f, 1,
                 lng ? 0x785AC8C4u : 0x4A5AC8C4u, D_TEXT);
        }
        // and one bright block running the rail
        {
            float p = (float)frame * (side ? -0.9f : 0.9f);
            p = p - span * (float)((int)(p / span));
            if (p < 0.0f) p += span;
            for (int t = 0; t < 5; t++) {
                const float q = p + (side ? t : -t) * 2.0f;
                if (q < 0.0f || q > span) continue;
                const unsigned int a = (unsigned int)(200 - t * 40);
                fill(rx + 1, top + q, 3, 2, (a << 24) | 0x0078F0E2u, D_TEXT);
            }
        }
    }

    // blinking record dot
    if (((frame / 8) & 1) == 0) {
        fill(190, 7, 4, 4, 0xDCFF785Au, D_TEXT);
    }
}


// The condition block as a readout rather than a plain box: a fine measuring
// grid, a sweep column following the trace's head, and a little interference.
// Everything is a function of the frame counter, and it all draws BEHIND the
// EKG (which the engine submits at 660) so the trace stays the brightest thing
// in the block.
//
// Reference is RE2 Remake's health gauge: a dark panel, a thick coloured line,
// nothing competing with it. The grid is kept near the floor of what is
// visible for that reason - it should read as the surface the trace is drawn
// on, not as pattern.
static void ecg_screen(int frame, int head1000, unsigned int tint)
{
    const float x0 = ECG_EKG_X, y0 = ECG_EKG_Y;
    const float x1 = ECG_EKG_X + ECG_EKG_W, y1 = ECG_EKG_Y + ECG_EKG_H;

    // measuring grid
    for (int i = 1; i < 10; i++) {
        fill(x0 + i * 9.0f, y0, 1, y1 - y0, 0x1A3C7A78u, D_PLATE - 2u);
    }
    for (int i = 1; i < 5; i++) {
        const float gy = y0 + i * 5.0f;
        if (gy >= y1 - 1.0f) break;
        fill(x0, gy, x1 - x0, 1, 0x163C7A78u, D_PLATE - 2u);
    }
    // the line that means something: where a flat trace actually sits
    fill(x0, y0 + (float)ECG_EKG_BASE, x1 - x0, 1, 0x32509894u, D_PLATE - 2u);

    // corner ticks, so the grid reads as a framed instrument
    const unsigned int tc = 0x6E5AAAA6u;
    for (int i = 0; i < 4; i++) {
        const float cx = (i & 1) ? x1 - 4.0f : x0;
        const float cy = (i & 2) ? y1 - 1.0f : y0;
        fill(cx, cy, 4, 1, tc, D_PLATE - 2u);
    }

    // sweep column: a bright edge where the trace is being written, with two
    // dimmer columns trailing it. The position comes from the game's own sweep
    // (DAT_00ae9f36, passed in as a permille of the trace) rather than a period
    // of this function's own, so the column cannot drift off the trace's head.
    int hp = head1000;
    if (hp < 0) hp = 0;
    if (hp > 1000) hp = 1000;
    const float head = x0 + (float)hp * (x1 - x0) / 1000.0f;
    // The two trailing columns are behind the head, so at the start of a sweep
    // they fall outside the band - and being tinted with the condition colour
    // they showed up as stray green bars to the left of the block's frame.
    // Nothing here may leave [x0, x1].
    static const float kTrail[3] = { 0.0f, 3.0f, 6.0f };
    static const unsigned int kTrailA[3] = { 0x66000000u, 0x2E000000u, 0x1A000000u };
    for (int t = 0; t < 3; t++) {
        const float cx = head - kTrail[t];
        if (cx < x0 || cx > x1 - 1.0f) continue;
        fill(cx, y0, 1, y1 - y0, (tint & 0x00FFFFFFu) | kTrailA[t], D_PLATE - 2u);
    }

    // interference: two tear bars drifting at different rates, and a speckle
    // row that moves with them. Cheap, and it never sits still long enough to
    // read as a fixed pattern.
    for (int b = 0; b < 2; b++) {
        const int per = (b == 0) ? 47 : 83;
        const int p = (frame * (b + 2)) % per;
        if (p < 6) {                        // only visible a few frames in
            const float by = y0 + (float)((frame / per + b * 7) % (int)(y1 - y0));
            fill(x0, by, x1 - x0, 1, 0x1C78DCD8u, D_PLATE - 2u);
            fill(x0 + (float)((frame * 3) % 40), by, 18, 1, 0x3278DCD8u,
                 D_PLATE - 2u);
        }
    }
    for (int i = 0; i < 6; i++) {
        const int seed = (frame / 3 + i * 29) * 1103515245 + 12345;
        const float sx = x0 + (float)(((seed >> 16) & 0x7FFF) % (int)(x1 - x0));
        const float sy = y0 + (float)(((seed >> 8) & 0x7F) % (int)(y1 - y0));
        fill(sx, sy, 1, 1, 0x2E78DCD8u, D_PLATE - 2u);
    }
}


// The item's action menu, beside the selected cell.
//
// RE1 puts a 48x24 sprite box at a fixed (0x90, 0x39) - over the middle of the
// original two-column grid - and the three options live in it as pre-drawn
// art. The skin opens a panel next to the cell the cursor is on instead, which
// is what RE2 Remake does; overlapping the neighbouring cells is part of that
// look, not a problem to design around. The panel flips to the left of the
// cell when there is no room on the right, so the last two columns still work.
//
// `open` is the game's own counter, 0..8, so the panel grows and shrinks on
// exactly the frames the original box did and the timing of the sound effects
// still matches.
static void action_menu(const struct UiSkinState* st)
{
    if (st->actionOpen <= 0 || st->selected < 0) return;

    static const char* const kRows[3] = { "USE", "CHECK", "COMBINE" };
    static const AchvRect* const kIcons[3] = {
        &g_achvMarkIconuse, &g_achvMarkIconcheck, &g_achvMarkIconcombine
    };
    const char* row0 = st->actionEquip ? "EQUIP" : kRows[0];
    const AchvRect* icon0 = st->actionEquip ? &g_achvMarkIconequip : kIcons[0];

    const float cx = GRID_X + (st->selected % GRID_COLS) * CELL_GX;
    const float cy = GRID_Y + (st->selected / GRID_COLS) * CELL_GY;

    // pad is the plate showing above the first row and below the last, inside
    // the border - the same on both edges, so the rows sit centred in the box.
    // The options are a choice the player is making, so they read a size up
    // from the screen's labels - 9 game pixels against the usual 7.
    const float ACTION_TEXT_K = text_k(UI_FONT_BODY) * (9.0f / (float)T_SMALL);
    // Wider than the labels need: the icon column is 18 px of it.
    const float fullW = 82.0f, rowH = 14.0f, pad = 2.0f;
    const float fullH = rowH * 3.0f + pad * 2.0f;

    // grow from the cell's edge: width first, then the rows fade in
    const float t = (float)(st->actionOpen > 8 ? 8 : st->actionOpen) / 8.0f;
    const float w = fullW * t;
    const float h = fullH * (0.4f + 0.6f * t);

    const int left = (cx + CELL_W + 4.0f + fullW > 313.0f);
    const float x = left ? (cx - 4.0f - w) : (cx + CELL_W + 4.0f);
    // Top-aligned with the cell, the way RE2 Remake hangs it - except on the
    // bottom rows, where it would run past the grid into the info card, so
    // there it hangs from the cell's bottom edge instead.
    float y = cy;
    const float gridBottom = GRID_Y + (GRID_ROWS - 1) * CELL_GY + CELL_H;
    if (y + fullH > gridBottom) y = cy + CELL_H - h;
    if (y < GRID_Y) y = GRID_Y;

    // Opaque, not a translucent plate like the rest of the skin: this panel
    // sits ON TOP of the inventory, so anything showing through it is an item
    // icon or a cell border cutting across the words. The scrim look belongs
    // to the surfaces the icons sit on, not to the menu that covers them.
    fill(x, y, w, h, 0xFF06141Au, D_MENU);
    frame_rect(x, y, w, h, 0xFF78F0E2u, D_MENU);
    if (t < 0.99f) return;          // rows only once the panel is open

    // a tick joining the panel to the cell it belongs to
    const float jx = left ? (x + w) : (x - 4.0f);
    fill(jx, cy + CELL_H * 0.5f, 4, 1, 0xCD78F0E2u, D_MENU);

    for (int i = 0; i < 3; i++) {
        const float ry = y + pad + i * rowH;
        const char* label = (i == 0) ? row0 : kRows[i];
        const AchvRect* icon = (i == 0) ? icon0 : kIcons[i];
        const int sel = (i == st->actionIndex);
        // the icon sits in its own column left of the label, as in RE2 Remake
        // 10x10 is the icon's own size in the atlas: drawn at anything else
        // the nearest-neighbour step falls unevenly and the shape wobbles.
        pixel_mark(icon, x + 4.0f, ry + 1.5f, 10, 10,
                   sel ? 0xFF06161Au : 0xD2B4E0DCu, D_MENU_TX - 1u);
        if (i == st->actionIndex) {
            // the selected row is a filled bar with dark text, as in RE2R
            // Full row height: with rowH - 1 the last row left a dark strip
            // between it and the panel's bottom border, which read as the
            // plate not reaching its own edge.
            fill(x + 1.0f, ry, w - 2.0f, rowH, 0xFFA8E6DCu, D_MENU_TX);
            UiAtlas_TextPushed(UI_FONT_BODY, label,
                               (x + 18.0f) * s_sx, (ry + 2.0f) * s_sy,
                               ACTION_TEXT_K * s_sy, 0xFF06161Au,
                               D_MENU_TX - 1u);
        } else {
            UiAtlas_TextPushed(UI_FONT_BODY, label,
                               (x + 18.0f) * s_sx, (ry + 2.0f) * s_sy,
                               ACTION_TEXT_K * s_sy, 0xD2B4E0DCu,
                               D_MENU_TX);
        }
    }
}

// ---------------------------------------------------------------------------
void UiSkin_DrawStatus(const struct UiSkinState* st)
{
    if (!s_enabled || st == NULL) return;
    if (!UiAtlas_Ready()) return;

    skin_begin();

    // the frozen room, dimmed. Without a scrim the item icons - blocky 40x30
    // pixel art - lose contrast against a lit room.
    fill(0, 0, 320, 240, C_SCRIM, D_SCRIM);

    filler(st->frame);

    // --- tabs ---------------------------------------------------------------
    static const char* const kTabs[4] = { "MAP", "FILE", "RADIO", "EXIT" };
    for (int i = 0; i < 4; i++) {
        const float x = 200.0f + i * 29.0f;
        const float w = label_w(UI_FONT_BODY, kTabs[i]);
        const int off = (i == 2 && !st->radioOn);
        const unsigned int col = off ? 0x783C8282u
                                     : (i == st->tab ? C_TEXT : C_LABEL);
        if (i == st->tab) {
            // the selected tab gets the plate the original gave it, kept flat
            fill(x - 2, 3, 31, 13, 0x6614464Au, D_PLATE);
            fill(x - 2, 15, 31, 1, C_EDGE, D_TEXT);
        }
        label(UI_FONT_BODY, kTabs[i], x + (27.0f - w) * 0.5f, 6, col);
    }
    fill(GRID_X, 19, 312 - GRID_X, 1, C_LINE, D_PLATE);

    // --- the item grid ------------------------------------------------------
    for (int i = 0; i < GRID_COLS * GRID_ROWS; i++) {
        const float x = GRID_X + (i % GRID_COLS) * CELL_GX;
        const float y = GRID_Y + (i / GRID_COLS) * CELL_GY;
        const int live = (i < st->slotCount);
        cell(x, y, CELL_W, CELL_H, live && i < st->heldCount, i == st->selected);
        if (live && i == st->equipped) {
            label(UI_FONT_BODY, "E", x + 3, y + 1, C_TEXT);
        }
    }

    // cursor furniture: brackets and side markers, in front of the icons
    if (st->selected >= 0 && st->selected < GRID_COLS * GRID_ROWS) {
        const float x = GRID_X + (st->selected % GRID_COLS) * CELL_GX;
        const float y = GRID_Y + (st->selected / GRID_COLS) * CELL_GY;
        const float b = 6.0f;
        fill(x - 2, y - 2, b, 1, C_EDGE, D_FRONT);
        fill(x - 2, y - 2, 1, b, C_EDGE, D_FRONT);
        fill(x + CELL_W + 2 - b, y - 2, b, 1, C_EDGE, D_FRONT);
        fill(x + CELL_W + 1, y - 2, 1, b, C_EDGE, D_FRONT);
        fill(x - 2, y + CELL_H + 1, b, 1, C_EDGE, D_FRONT);
        fill(x - 2, y + CELL_H + 2 - b, 1, b, C_EDGE, D_FRONT);
        fill(x + CELL_W + 2 - b, y + CELL_H + 1, b, 1, C_EDGE, D_FRONT);
        fill(x + CELL_W + 1, y + CELL_H + 2 - b, 1, b, C_EDGE, D_FRONT);

        const unsigned int pulse = (((st->frame / 5) & 1) == 0) ? C_ORANGE : 0x78FF9650u;
        mark(&g_achvMarkTriangle, x - 6, y + CELL_H * 0.5f - 2, 4, 4, pulse, D_FRONT);
        mark(&g_achvMarkTriangle, x + CELL_W + 2, y + CELL_H * 0.5f - 2, 4, 4, pulse, D_FRONT);
    }

    glint(GRID_X - 2, GRID_Y - 2,
          GRID_COLS * CELL_GX - (CELL_GX - CELL_W) + 4,
          GRID_ROWS * CELL_GY - (CELL_GY - CELL_H) + 4,
          st->frame, 2.2f, 26, 0x00AAFFF6u);

    // --- the info card ------------------------------------------------------
    fill(GRID_X, RULE_Y, 312 - GRID_X, 1, C_LINE, D_PLATE);
    if (st->itemName != NULL) {
        fill(CARD_X0, NAME_Y - 3, CARD_X1 - CARD_X0, 226 - (NAME_Y - 3), C_CARD, D_PLATE);
        frame_rect(CARD_X0, NAME_Y - 3, CARD_X1 - CARD_X0, 226 - (NAME_Y - 3),
                   0x785AB4B0u, D_PLATE);
        // A hatched corner, the way every panel in the reference is cut. Five
        // stepped runs read as a diagonal at this size and cost five quads
        // rather than the fifty a real stroked diagonal would.
        for (int i = 0; i < 5; i++) {
            const float len = 12.0f - i * 2.0f;
            fill(CARD_X1 - 2.0f - len, NAME_Y - 1.0f + i * 2.0f, len, 1,
                 0x3C5AB4B0u, D_TEXT);
        }
        label(UI_FONT_TITLE, st->itemName, GRID_X, NAME_Y, C_TEXT);
        fill(CARD_X0, NAME_Y + 12, CARD_X1 - CARD_X0 - 1, 2, C_AMBER, D_TEXT);
        if (st->itemType != NULL) {
            fill(CARD_X0, NAME_Y + 15, 46, 9, 0xAA286C6Cu, D_TEXT);
            label(UI_FONT_BODY, st->itemType, CARD_X0 + 4, NAME_Y + 16, C_TEXT);
        }
        // id chip and code readout at the right end of the name line
        {
            const float chipW = 22.0f;
            const float chipX = CARD_X1 - 46.0f;
            char id[8];
            snprintf(id, sizeof(id), "# %02d",
                     (st->selected >= 0) ? st->selected + 1 : 0);
            fill(chipX, NAME_Y + 3, chipW, 8, 0xD278F0E2u, D_TEXT);
            label(UI_FONT_BODY, id, chipX + 3, NAME_Y + 4, 0xFF061416u);
            char code[16];
            snprintf(code, sizeof(code), "00:%05d",
                     10000 + ((st->selected >= 0) ? st->selected * 17 : 0));
            label(UI_FONT_BODY, code, chipX + chipW + 2, NAME_Y + 4, C_DIM);
        }
        // The item's own examine text (g_ItemDescriptions), decoded to ASCII by
        // MainMenu.cpp and re-wrapped here. The table's line break sits where
        // the original's 26-column line ran out; the card is far wider, so
        // honouring it would leave most descriptions as two short stubs.
        // While the item viewer is open the card is where that text is being
        // read, so it steps up to the title font - same words, the size the
        // reading deserves.
        if (st->itemDesc != NULL) {
            const int font = st->descLarge ? UI_FONT_TITLE : UI_FONT_BODY;
            const float width = (float)(CARD_X1 - 4) - GRID_X;
            char lines[3][WRAP_MAX_CHARS];
            const int n = wrap_text(font, st->itemDesc, width, lines, 3);
            const float step = st->descLarge ? 13.0f : 10.0f;
            const float top  = st->descLarge ? (NAME_Y + 25.0f) : (NAME_Y + 28.0f);
            for (int i = 0; i < n; i++) {
                if (st->descLarge) {
                    UiAtlas_TextPushed(UI_FONT_TITLE, lines[i], GRID_X * s_sx,
                                       (top + i * step) * s_sy,
                                       text_k(UI_FONT_TITLE) * 0.95f * s_sy,
                                       C_TEXT, D_TEXT);
                } else {
                    label(UI_FONT_BODY, lines[i], GRID_X, top + i * step, C_BODY);
                }
            }
        }
    }

    // capacity bar under the card
    for (int i = 0; i < 16; i++) {
        const int on = i < st->heldCount;
        fill(GRID_X + i * 11.0f, 228, 8, 3,
             on ? 0xD278F0E2u : 0x50467878u, D_TEXT);
    }
    label(UI_FONT_BODY, "CAPACITY", GRID_X, 233, C_DIM);
    {
        char cap[16];
        snprintf(cap, sizeof(cap), "%d / %d", st->heldCount, st->slotCount);
        label_right(UI_FONT_BODY, cap, 312, 233, C_DIM);
    }

    // --- left column --------------------------------------------------------
    label(UI_FONT_BODY, "CHARACTER", 8, 14, C_LABEL);
    cell(PORTRAIT_X, PORTRAIT_Y, 38, CELL_H, 1, 0);
    if (st->characterName) label(UI_FONT_TITLE, st->characterName, 50, 26, C_TEXT);
    if (st->affiliation)   label(UI_FONT_BODY, st->affiliation, 50, 39, C_LABEL);

    label(UI_FONT_BODY, "EQUIPPED", 8, 62, C_LABEL);
    cell(EQUIP_X, EQUIP_Y, CELL_W, CELL_H, st->equipped >= 0, 0);

    label(UI_FONT_BODY, "OTHER", 8, 110, C_LABEL);
    cell(OTHER_X, OTHER_Y, CELL_W, CELL_H, st->other >= 0, 0);

    // condition: same cell frame, trace inside, status word under it
    label(UI_FONT_BODY, "CONDITION", ECG_X, ECG_Y - 8, C_LABEL);
    cell(ECG_X, ECG_Y, ECG_W, ECG_H, 1, 0);
    ecg_screen(st->frame, st->ekgHead, st->conditionColor);
    {
        // No synthetic trace here: the game draws a real EKG of its own
        // (menu_draw_health_bar), wave tables and all, and MainMenu.cpp
        // remaps its line primitives into UiSkin_EkgRect below - so what is
        // in this block is the game's own heartbeat, stretched to fit. It
        // sweeps across the block and fades behind itself, exactly as it does
        // on the original screen, so there is no baseline to draw either.
        if (st->condition) {
            const float k = text_k(UI_FONT_TITLE);
            UiAtlas_TextPushed(UI_FONT_TITLE, st->condition,
                               (ECG_X + 5) * s_sx, (ECG_Y + ECG_H - 11) * s_sy,
                               k * s_sy, st->conditionColor, D_TEXT);
        }
    }

    // points, then the pip rows
    label(UI_FONT_BODY, "POINTS", 8, 217, C_LABEL);
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", st->points);
        const float k = text_k(UI_FONT_TITLE);
        const float w = UiAtlas_TextWidth(UI_FONT_TITLE, buf, k);
        UiAtlas_TextPushed(UI_FONT_TITLE, buf, (102.0f - w) * s_sx, 214 * s_sy,
                           k * s_sy, C_TEXT, D_TEXT);
    }
    // the diamond row is the achievement profile: one per achievement, lit
    // for each unlocked. The ring row stays decorative.
    {
        int n = st->achvTotal, lit = st->achvUnlocked;
        if (n <= 0) { n = 6; lit = 4; }
        if (n > 10) n = 10;
        if (lit > n) lit = n;
        pip_row(10, 226, n, lit, &g_achvMarkDiamond, C_EDGE, 0x6E326E6Eu);
    }
    pip_row(56, 226, 4, 2, &g_achvMarkRing, C_AMBER, 0x6E5A5040u);

    action_menu(st);

    // The message banner. RE1's menu messages are drawn by the message system
    // itself, in the game's own 8x14 font, and there is no getting in front of
    // them - so the skin gives them a floor to stand on instead of letting
    // them fall across the condition block and the card.
    if (st->msgY > 0) {
        const float top = (float)st->msgY - 6.0f;
        const float h = 14.0f * 2.0f + 10.0f;
        fill(4, top, 312, h, 0xF2040C10u, D_MSG_BACK);
        fill(4, top, 312, 1, 0xC878F0E2u, D_MSG_BACK);
        fill(4, top + h - 1, 312, 1, 0xC878F0E2u, D_MSG_BACK);
        fill(4, top, 1, h, 0x8C5AB4B0u, D_MSG_BACK);
        fill(315, top, 1, h, 0x8C5AB4B0u, D_MSG_BACK);
        // a corner tick at each end, so the band belongs to the same UI
        for (int i = 0; i < 2; i++) {
            const float bx = i ? 309.0f : 4.0f;
            fill(bx, top + 2, 3, 1, 0xC878F0E2u, D_MSG_BACK);
            fill(bx, top + h - 3, 3, 1, 0xC878F0E2u, D_MSG_BACK);
        }
    }
}
