// EditorUICore.cpp - the frame, identity, hit testing and the deferred layers.
//
// CUSTOM (port-only).
//
// THE FRAME
//
// BeginFrame samples the window and the mouse once and hands the same numbers
// to every widget, so two widgets asked about the same pixel always agree.
// EndFrame draws what has to land on top of everything - an open menu, a
// combo's list, a tooltip - which is why a panel can put up a menu without
// knowing anything about the panel that will be drawn after it.
//
// IDENTITY
//
// hot / active is the whole of the interaction model. `hot` is what the cursor
// is over, recomputed every frame; `active` is what a press captured and holds
// until the button comes back up, which is what makes a drag survive the
// cursor leaving the widget. Both are hashes of a string and an index, because
// the things being edited are C arrays that a reload replaces - see the note
// in EditorUI.h.
#include "EditorUIInternal.h"
#include "../EditorState.h"
#include "../../../Globals.h"
#include "../../../marni/MarniSystem.h"
#include "../../../platform/platform.h"

#include <string.h>
#include <stdio.h>

EdUiCtx g_edui;

// ---------------------------------------------------------------------------
// Rectangles
// ---------------------------------------------------------------------------
EdRect EdR(float x, float y, float w, float h)
{
    EdRect r; r.x = x; r.y = y; r.w = w; r.h = h; return r;
}

EdRect EdR_Inset(EdRect r, float by)
{
    r.x += by; r.y += by;
    r.w -= by * 2.0f; r.h -= by * 2.0f;
    if (r.w < 0.0f) r.w = 0.0f;
    if (r.h < 0.0f) r.h = 0.0f;
    return r;
}

// Carve a strip off one side of *r and return it, the way every dock layout in
// this editor is built: cut the bars off the window, and whatever is left is
// the viewport.
EdRect EdR_Cut(EdRect* r, float amount, int side)
{
    EdRect out = *r;
    if (amount < 0.0f) amount = 0.0f;
    switch (side) {
    case ED_SIDE_TOP:
        if (amount > r->h) amount = r->h;
        out.h = amount; r->y += amount; r->h -= amount; break;
    case ED_SIDE_BOTTOM:
        if (amount > r->h) amount = r->h;
        out.y = r->y + r->h - amount; out.h = amount; r->h -= amount; break;
    case ED_SIDE_LEFT:
        if (amount > r->w) amount = r->w;
        out.w = amount; r->x += amount; r->w -= amount; break;
    default:
        if (amount > r->w) amount = r->w;
        out.x = r->x + r->w - amount; out.w = amount; r->w -= amount; break;
    }
    return out;
}

int EdR_Has(EdRect r, float x, float y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
EdId EdUI_Id(const char* key)
{
    unsigned int h = 2166136261u;            // FNV-1a
    if (key) for (; *key; key++) { h ^= (unsigned char)*key; h *= 16777619u; }
    return h ? h : 1u;                       // 0 means "nothing"
}

EdId EdUI_IdIdx(const char* key, int index)
{
    unsigned int h = EdUI_Id(key);
    h ^= (unsigned int)index * 2654435761u;
    h *= 16777619u;
    return h ? h : 1u;
}

// ---------------------------------------------------------------------------
// The keyboard.
//
// The game polls rather than queues (plat_key_state), so edges are kept here.
// Characters do come as messages, because a text field needs the layout's
// interpretation of a keystroke and not its scan code.
// ---------------------------------------------------------------------------
#define EDUI_VK_COUNT 256
static unsigned char s_keyWas[EDUI_VK_COUNT];
static unsigned char s_keyNow[EDUI_VK_COUNT];

int EdUiKey_Held(int vk)
{
    if (vk < 0 || vk >= EDUI_VK_COUNT) return 0;
    return s_keyNow[vk];
}

int EdUiKey_Pressed(int vk)
{
    if (vk < 0 || vk >= EDUI_VK_COUNT) return 0;
    return s_keyNow[vk] && !s_keyWas[vk];
}

int EdUiKey_Char(void)
{
    return EditorInput_TakeChar();
}

static void edui_keys_sample(void)
{
    // Only the keys the interface reacts to: polling 256 of them every frame
    // for the handful that matter is a syscall per key on Windows.
    static const int watch[] = {
        VK_RETURN, VK_ESCAPE, VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT,
        VK_UP, VK_DOWN, VK_HOME, VK_END, VK_TAB, VK_CONTROL, VK_SHIFT,
        VK_MENU, 'A', 'C', 'V', 'X', 'Z', 'Y', 'S', 'D', 'F', 'G',
        'W', 'Q', 'E', 'R', '1', '2', '3', '4'
    };
    memcpy(s_keyWas, s_keyNow, sizeof(s_keyWas));
    memset(s_keyNow, 0, sizeof(s_keyNow));
    for (unsigned i = 0; i < sizeof(watch) / sizeof(watch[0]); i++) {
        const int vk = watch[i];
        s_keyNow[vk] = (unsigned char)((plat_key_state(vk) & 0x8000) != 0);
    }
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------
int EdUI_Ready(void)      { return g_edui.ready; }
float EdUI_Scale(void)    { return g_edui.scale; }
float EdUI_Width(void)    { return g_edui.w; }
float EdUI_Height(void)   { return g_edui.h; }
float EdUI_MouseX(void)   { return g_edui.mx; }
float EdUI_MouseY(void)   { return g_edui.my; }
int   EdUI_WantsMouse(void)    { return g_edui.wantsMouse; }
int   EdUI_IsHot(EdId id)      { return g_edui.hot == id; }
int   EdUI_Clickable(EdId id, EdRect r, int* outHeld)
{
    return EdUiCore_Clicked(id, r, outHeld);
}
int   EdUI_WantsKeyboard(void) { return g_edui.focus != 0; }

// One design pixel is this many backbuffer pixels.
//
// The interface is laid out for a 1080p window and scaled from there. The
// clamp is what keeps it usable at both ends: below 0.8 the 14px font stops
// being readable, and above that a 4K window would otherwise get panels with
// the proportions of a phone.
static float edui_scale_for(float bbH)
{
    float k = bbH / 1080.0f;
    if (k < 0.80f) k = 0.80f;
    if (k > 2.50f) k = 2.50f;
    return k;
}

void EdUI_BeginFrame(void)
{
    // A zeroed context would mean "menu 0 is open", and the editor would come
    // up with the File menu down. Nothing else in the context needs an initial
    // value, so this is the whole of the toolkit's initialisation.
    static int s_started = 0;
    if (!s_started) {
        s_started = 1;
        g_edui.menuOpen = -1;
        g_edui.menuPicked = -1;
        g_edui.comboPicked = -1;
    }

    DWORD bw = 0, bh = 0;
    MarniGetBackBufferSize(&bw, &bh);
    if (bw < 320) bw = 320;
    if (bh < 240) bh = 240;

    g_edui.ready = EdUiDraw_Ready();
    g_edui.scale = edui_scale_for((float)bh);
    g_edui.w = (float)bw / g_edui.scale;
    g_edui.h = (float)bh / g_edui.scale;

    const float px = g_edui.mx;
    const float py = g_edui.my;
    g_edui.mx = g_edMouse.bbX / g_edui.scale;
    g_edui.my = g_edMouse.bbY / g_edui.scale;
    g_edui.dmx = g_edui.mx - px;
    g_edui.dmy = g_edui.my - py;

    for (int i = 0; i < 3; i++) {
        g_edui.mheld[i]     = g_edMouse.held[i];
        g_edui.mpressed[i]  = g_edMouse.pressed[i];
        g_edui.mreleased[i] = g_edMouse.released[i];
    }
    g_edui.wheel = (float)g_edMouse.wheel / 120.0f;

    edui_keys_sample();

    g_edui.hot = g_edui.hotNext;
    g_edui.hotNext = 0;
    g_edui.clipTop = 0;
    g_edui.wantsMouse = 0;
    g_edui.tip[0] = '\0';
    g_edui.menuIndex = -1;
    g_edui.menuPicked = -1;
    g_edui.comboPicked = -1;
    g_edui.editBlink++;

    // A button released anywhere ends the drag it started, even if the cursor
    // has since left the widget - that is the point of `active`.
    if (!g_edui.mheld[0] && g_edui.active != 0) g_edui.active = 0;

    for (int i = 0; i < EDUI_SCROLL_SLOTS; i++) g_edui.scroll[i].used = 0;
}

void EdUI_PushClip(EdRect r)
{
    if (g_edui.clipTop >= EDUI_CLIP_STACK) return;
    // Intersect with what is already there: a list inside a panel is clipped
    // by both, and the inner one asking for more than the outer allows is a
    // layout bug that should show as nothing rather than as overdraw.
    const EdRect p = EdUiDraw_Clip();
    float x0 = r.x > p.x ? r.x : p.x;
    float y0 = r.y > p.y ? r.y : p.y;
    float x1 = (r.x + r.w < p.x + p.w) ? r.x + r.w : p.x + p.w;
    float y1 = (r.y + r.h < p.y + p.h) ? r.y + r.h : p.y + p.h;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    g_edui.clip[g_edui.clipTop++] = EdR(x0, y0, x1 - x0, y1 - y0);
}

void EdUI_PopClip(void)
{
    if (g_edui.clipTop > 0) g_edui.clipTop--;
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------
static int edui_overlay_open(void)
{
    return g_edui.menuOpen >= 0 || g_edui.comboOpen != 0;
}

static int s_overlayPass = 0;

int EdUiCore_Hover(EdId id, EdRect r)
{
    if (!g_edui.ready) return 0;

    // While something is being dragged nothing else can become hot: a slider
    // dragged past a button must not light the button up on the way.
    if (g_edui.active != 0 && g_edui.active != id) return 0;

    // An open menu covers what is under it. Only the deferred pass, which is
    // what draws the menu, may take the cursor while one is up.
    if (edui_overlay_open() && !s_overlayPass) {
        g_edui.wantsMouse = 1;
        return 0;
    }

    if (!EdR_Has(r, g_edui.mx, g_edui.my)) return 0;
    if (!EdR_Has(EdUiDraw_Clip(), g_edui.mx, g_edui.my)) return 0;

    g_edui.hotNext = id;
    g_edui.wantsMouse = 1;
    return 1;
}

// The standard press/release: a click counts only when the press and the
// release both happened on the widget, which is what lets a mis-press be taken
// back by dragging off before letting go.
int EdUiCore_Clicked(EdId id, EdRect r, int* outHeld)
{
    const int over = EdUiCore_Hover(id, r);
    int clicked = 0;

    if (over && g_edui.mpressed[0]) {
        g_edui.active = id;
        g_edui.focus = 0;              // clicking anything drops the caret
        g_edui.dragX = g_edui.mx;
        g_edui.dragY = g_edui.my;
    }
    if (g_edui.active == id && g_edui.mreleased[0]) {
        if (over) clicked = 1;
        g_edui.active = 0;
    }
    if (outHeld) *outHeld = (g_edui.active == id) ? 1 : 0;
    return clicked;
}

// ---------------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------------
static EdUiScroll* scroll_slot(EdId id)
{
    int free_i = -1;
    for (int i = 0; i < EDUI_SCROLL_SLOTS; i++) {
        if (g_edui.scroll[i].id == id) return &g_edui.scroll[i];
        if (free_i < 0 && g_edui.scroll[i].id == 0) free_i = i;
    }
    if (free_i < 0) free_i = 0;        // recycle: a list nobody scrolls is fine
    EdUiScroll* s = &g_edui.scroll[free_i];
    s->id = id; s->offset = 0.0f; s->contentH = 0.0f; s->viewH = 0.0f;
    return s;
}

static EdRect s_scrollOuter[EDUI_CLIP_STACK];
static int    s_scrollTop = 0;

EdRect EdUI_ScrollBegin(EdId id, EdRect r, float contentH)
{
    EdUiScroll* s = scroll_slot(id);
    s->used = 1;
    s->contentH = contentH;
    s->viewH = r.h;

    const int needsBar = contentH > r.h + 0.5f;
    EdRect body = r;
    if (needsBar) body.w -= EDM_SCROLL_W;

    // The wheel scrolls whatever the cursor is over, which is what every list
    // anywhere does, and needs no click first.
    if (EdR_Has(r, g_edui.mx, g_edui.my) && g_edui.wheel != 0.0f && needsBar
        && !edui_overlay_open()) {
        s->offset -= g_edui.wheel * EDM_ROW_H * 3.0f;
        g_edui.wantsMouse = 1;
    }

    const float maxOff = (contentH > r.h) ? contentH - r.h : 0.0f;
    if (s->offset > maxOff) s->offset = maxOff;
    if (s->offset < 0.0f)   s->offset = 0.0f;

    if (needsBar) {
        const EdRect track = EdR(r.x + r.w - EDM_SCROLL_W, r.y,
                                 EDM_SCROLL_W, r.h);
        EdUI_Fill(track, 0x22000000u);

        const float th = (r.h / contentH) * r.h;
        const float thumbH = th < 24.0f ? 24.0f : th;
        const float ty = r.y + (maxOff > 0.0f
                        ? (s->offset / maxOff) * (r.h - thumbH) : 0.0f);
        const EdRect thumb = EdR(track.x + 2.0f, ty, EDM_SCROLL_W - 4.0f, thumbH);

        const EdId tid = id ^ 0x5CB0u;
        int held = 0;
        const int over = EdUiCore_Hover(tid, track);
        if (over && g_edui.mpressed[0]) {
            g_edui.active = tid;
            g_edui.dragY = g_edui.my;
            g_edui.dragA = s->offset;
            // Clicking the track jumps the thumb to the cursor rather than
            // paging: with one screen of content a page is the whole list.
            if (!EdR_Has(thumb, g_edui.mx, g_edui.my)) {
                const float want = ((g_edui.my - r.y - thumbH * 0.5f)
                                  / (r.h - thumbH)) * maxOff;
                s->offset = want;
                g_edui.dragA = want;
            }
        }
        if (g_edui.active == tid) {
            held = 1;
            const float dy = g_edui.my - g_edui.dragY;
            s->offset = g_edui.dragA + (dy / (r.h - thumbH)) * maxOff;
            if (s->offset > maxOff) s->offset = maxOff;
            if (s->offset < 0.0f)   s->offset = 0.0f;
        }
        EdUI_Plate(thumb, EDUI_TILE_ROUND3,
                   held ? EDC_ACCENT : (over ? 0xFF6A7078u : 0xFF454A50u));
    } else {
        s->offset = 0.0f;
    }

    if (s_scrollTop < EDUI_CLIP_STACK) s_scrollOuter[s_scrollTop++] = r;
    EdUI_PushClip(body);
    return EdR(body.x, body.y - s->offset, body.w, contentH);
}

void EdUI_ScrollEnd(void)
{
    EdUI_PopClip();
    if (s_scrollTop > 0) s_scrollTop--;
}

// ---------------------------------------------------------------------------
// Splitters
// ---------------------------------------------------------------------------
static int edui_splitter(EdId id, EdRect r, float* value, float lo, float hi,
                         int vertical)
{
    int held = 0;
    const int over = EdUiCore_Hover(id, r);
    if (over && g_edui.mpressed[0]) {
        g_edui.active = id;
        g_edui.dragX = g_edui.mx;
        g_edui.dragY = g_edui.my;
        g_edui.dragA = *value;
    }
    if (g_edui.active == id) {
        held = 1;
        const float d = vertical ? (g_edui.mx - g_edui.dragX)
                                 : (g_edui.my - g_edui.dragY);
        float v = g_edui.dragA + d;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        *value = v;
    }
    EdUI_Fill(r, EDC_BORDER);
    if (over || held) {
        // A hairline of accent down the middle: enough to say "this moves"
        // without drawing a grip that would be visible at rest.
        if (vertical) EdUI_Fill(EdR(r.x + r.w * 0.5f - 0.5f, r.y, 1.0f, r.h),
                                held ? EDC_ACCENT : EDC_ACCENT_DIM);
        else          EdUI_Fill(EdR(r.x, r.y + r.h * 0.5f - 0.5f, r.w, 1.0f),
                                held ? EDC_ACCENT : EDC_ACCENT_DIM);
    }
    return held;
}

int EdUI_SplitterV(EdId id, EdRect r, float* value, float lo, float hi)
{
    return edui_splitter(id, r, value, lo, hi, 1);
}

int EdUI_SplitterH(EdId id, EdRect r, float* value, float lo, float hi)
{
    return edui_splitter(id, r, value, lo, hi, 0);
}

// ---------------------------------------------------------------------------
// The menu bar.
//
// Declared inline by the panels, drawn deferred by EndFrame: the titles are
// drawn where they are declared, the open list is remembered and drawn last so
// it lands over the panels below it.
// ---------------------------------------------------------------------------
void EdUI_MenuBarBegin(EdRect r)
{
    g_edui.menuBar = r;
    g_edui.menuIndex = -1;
    EdUI_Fill(r, EDC_MENUBAR);
    EdUI_Fill(EdR(r.x, r.y + r.h - 1.0f, r.w, 1.0f), EDC_BORDER);
}

static float s_menuX = 0.0f;

int EdUI_MenuBegin(const char* title)
{
    g_edui.menuIndex++;
    if (g_edui.menuIndex == 0) s_menuX = g_edui.menuBar.x + EDM_PAD;

    const float w = EdUI_TextW(EDUI_FONT_UI, title) + EDM_PAD * 2.0f;
    const EdRect r = EdR(s_menuX, g_edui.menuBar.y, w, g_edui.menuBar.h);
    s_menuX += w;

    const EdId id = EdUI_IdIdx("menubar", g_edui.menuIndex);
    const int over = EdUiCore_Hover(id, r);
    const int open = (g_edui.menuOpen == g_edui.menuIndex);

    if (over && g_edui.mpressed[0]) {
        g_edui.menuOpen = open ? -1 : g_edui.menuIndex;
        g_edui.menuAnchor = r;
    } else if (over && g_edui.menuOpen >= 0 && !open) {
        // Once one menu is open, sliding across the bar opens the next, the
        // way a menu bar has worked since Motif.
        g_edui.menuOpen = g_edui.menuIndex;
        g_edui.menuAnchor = r;
    }

    if (open || over) EdUI_Fill(r, open ? EDC_ACCENT_DIM : EDC_HOVER);
    EdUI_TextIn(EDUI_FONT_UI, title, r, ED_ALIGN_CENTRE, EDC_TEXT);

    if (open) {
        g_edui.menuAnchor = r;
        g_edui.menuCount = 0;
        return 1;
    }
    return 0;
}

static void menu_push(const char* label, const char* shortcut, int icon,
                      int enabled, int sep, int check)
{
    if (g_edui.menuCount >= EDUI_MENU_ITEMS) return;
    EdUiMenuItem* it = &g_edui.menuItem[g_edui.menuCount++];
    memset(it, 0, sizeof(*it));
    if (label)    { strncpy(it->label, label, sizeof(it->label) - 1); }
    if (shortcut) { strncpy(it->shortcut, shortcut, sizeof(it->shortcut) - 1); }
    it->icon = icon;
    it->enabled = enabled;
    it->separator = sep;
    it->check = check;
}

int EdUI_MenuItem(const char* label, const char* shortcut, int icon,
                  int enabled)
{
    const int index = g_edui.menuCount;
    menu_push(label, shortcut, icon, enabled, 0, -1);
    return (g_edui.menuPicked == index) ? 1 : 0;
}

void EdUI_MenuCheck(const char* label, const char* shortcut, int* value)
{
    const int index = g_edui.menuCount;
    menu_push(label, shortcut, -1, 1, 0, value && *value ? 1 : 0);
    if (g_edui.menuPicked == index && value) *value = !*value;
}

void EdUI_MenuSeparator(void)
{
    menu_push(NULL, NULL, -1, 0, 1, -1);
}

void EdUI_MenuEnd(void)   { }
void EdUI_MenuBarEnd(void) { }

// ---------------------------------------------------------------------------
// Tooltips
// ---------------------------------------------------------------------------
void EdUI_Tooltip(const char* text)
{
    if (!text || !*text) return;
    strncpy(g_edui.tip, text, sizeof(g_edui.tip) - 1);
    g_edui.tip[sizeof(g_edui.tip) - 1] = '\0';
    g_edui.tipX = g_edui.mx;
    g_edui.tipY = g_edui.my;
}

// ---------------------------------------------------------------------------
// The deferred pass
// ---------------------------------------------------------------------------
#define EDUI_MENU_ROW   22.0f
#define EDUI_MENU_SEP    7.0f

static void edui_draw_menu(void)
{
    if (g_edui.menuOpen < 0 || g_edui.menuCount <= 0) return;

    float w = 150.0f;
    float h = EDM_GAP * 2.0f;
    for (int i = 0; i < g_edui.menuCount; i++) {
        const EdUiMenuItem* it = &g_edui.menuItem[i];
        if (it->separator) { h += EDUI_MENU_SEP; continue; }
        h += EDUI_MENU_ROW;
        float need = EdUI_TextW(EDUI_FONT_UI, it->label) + 52.0f
                   + EdUI_TextW(EDUI_FONT_SMALL, it->shortcut);
        if (need > w) w = need;
    }

    EdRect box = EdR(g_edui.menuAnchor.x,
                     g_edui.menuAnchor.y + g_edui.menuAnchor.h, w, h);
    if (box.x + box.w > g_edui.w - 4.0f) box.x = g_edui.w - 4.0f - box.w;
    if (box.x < 4.0f) box.x = 4.0f;

    // Shadow first, then the plate: the shadow is a blurred tile stretched a
    // few pixels past the box on every side.
    EdUI_Plate(EdR(box.x - 6.0f, box.y - 4.0f, box.w + 12.0f, box.h + 12.0f),
               EDUI_TILE_SHADOW, 0x70000000u);
    EdUI_Plate(box, EDUI_TILE_ROUND3, EDC_POPUP);
    EdUI_Plate(box, EDUI_TILE_LINE3, 0xFF3A3E44u);

    float y = box.y + EDM_GAP;
    for (int i = 0; i < g_edui.menuCount; i++) {
        const EdUiMenuItem* it = &g_edui.menuItem[i];
        if (it->separator) {
            EdUI_Fill(EdR(box.x + 8.0f, y + EDUI_MENU_SEP * 0.5f,
                          box.w - 16.0f, 1.0f), EDC_SEP);
            y += EDUI_MENU_SEP;
            continue;
        }
        const EdRect row = EdR(box.x + 3.0f, y, box.w - 6.0f, EDUI_MENU_ROW);
        const EdId id = EdUI_IdIdx("menuitem", i);
        const int over = it->enabled ? EdUiCore_Hover(id, row) : 0;
        if (over) {
            EdUI_Plate(row, EDUI_TILE_ROUND3, EDC_ACCENT_DIM);
            if (g_edui.mreleased[0]) g_edui.menuPicked = i;
        }
        const unsigned int col = it->enabled ? EDC_TEXT : EDC_TEXT_FAINT;
        if (it->check >= 0) {
            if (it->check) EdUI_Icon(EdR(row.x + 6.0f, row.y + 3.0f, 16.0f, 16.0f),
                                     EDUI_ICON_CHECK, EDC_ACCENT);
        } else if (it->icon >= 0) {
            EdUI_Icon(EdR(row.x + 6.0f, row.y + 3.0f, 16.0f, 16.0f), it->icon, col);
        }
        EdUI_TextIn(EDUI_FONT_UI, it->label,
                    EdR(row.x + 28.0f, row.y, row.w - 36.0f, row.h),
                    ED_ALIGN_LEFT, col);
        if (it->shortcut[0]) {
            EdUI_TextIn(EDUI_FONT_SMALL, it->shortcut,
                        EdR(row.x, row.y, row.w - 10.0f, row.h),
                        ED_ALIGN_RIGHT, EDC_TEXT_FAINT);
        }
        y += EDUI_MENU_ROW;
    }

    // A press anywhere else, or a pick, closes it. The pick is applied on the
    // NEXT frame, where the declaring code asks EdUI_MenuItem again and gets
    // its 1 - which is what keeps the menu's contents and its answer in the
    // same place in the source.
    if (g_edui.mpressed[0] && !EdR_Has(box, g_edui.mx, g_edui.my)
        && !EdR_Has(g_edui.menuBar, g_edui.mx, g_edui.my)) {
        g_edui.menuOpen = -1;
    }
    if (g_edui.menuPicked >= 0 || EdUiKey_Pressed(VK_ESCAPE)) {
        g_edui.menuOpen = -1;
    }
}

static void edui_draw_combo(void)
{
    if (g_edui.comboOpen == 0 || g_edui.comboItems == NULL) return;

    const float rowH = EDM_ROW_H;
    float h = g_edui.comboCount * rowH + 6.0f;
    if (h > 320.0f) h = 320.0f;

    EdRect box = EdR(g_edui.comboRect.x,
                     g_edui.comboRect.y + g_edui.comboRect.h + 2.0f,
                     g_edui.comboRect.w, h);
    if (box.y + box.h > g_edui.h - 4.0f) {
        box.y = g_edui.comboRect.y - box.h - 2.0f;      // flip above
        if (box.y < 4.0f) box.y = 4.0f;
    }

    EdUI_Plate(EdR(box.x - 6.0f, box.y - 4.0f, box.w + 12.0f, box.h + 12.0f),
               EDUI_TILE_SHADOW, 0x70000000u);
    EdUI_Plate(box, EDUI_TILE_ROUND3, EDC_POPUP);
    EdUI_Plate(box, EDUI_TILE_LINE3, 0xFF3A3E44u);

    EdUI_PushClip(EdR_Inset(box, 3.0f));
    for (int i = 0; i < g_edui.comboCount; i++) {
        const EdRect row = EdR(box.x + 3.0f, box.y + 3.0f + i * rowH,
                               box.w - 6.0f, rowH);
        const EdId id = EdUI_IdIdx("comboitem", i);
        if (EdUiCore_Hover(id, row)) {
            EdUI_Plate(row, EDUI_TILE_ROUND3, EDC_ACCENT_DIM);
            if (g_edui.mreleased[0]) {
                g_edui.comboPicked = i;
                g_edui.comboOpen = 0;
            }
        }
        EdUI_TextIn(EDUI_FONT_UI, g_edui.comboItems[i],
                    EdR(row.x + 6.0f, row.y, row.w - 12.0f, row.h),
                    ED_ALIGN_LEFT, EDC_TEXT);
    }
    EdUI_PopClip();

    if ((g_edui.mpressed[0] && !EdR_Has(box, g_edui.mx, g_edui.my))
        || EdUiKey_Pressed(VK_ESCAPE)) {
        g_edui.comboOpen = 0;
    }
}

static void edui_draw_tip(void)
{
    if (g_edui.tip[0] == '\0') return;
    const float tw = EdUI_TextW(EDUI_FONT_SMALL, g_edui.tip);
    EdRect box = EdR(g_edui.tipX + 14.0f, g_edui.tipY + 20.0f,
                     tw + 14.0f, 20.0f);
    if (box.x + box.w > g_edui.w - 4.0f) box.x = g_edui.w - 4.0f - box.w;
    if (box.y + box.h > g_edui.h - 4.0f) box.y = g_edui.tipY - box.h - 6.0f;
    EdUI_Plate(EdR(box.x - 4.0f, box.y - 3.0f, box.w + 8.0f, box.h + 9.0f),
               EDUI_TILE_SHADOW, 0x60000000u);
    EdUI_Plate(box, EDUI_TILE_ROUND3, 0xFF33373Cu);
    EdUI_TextIn(EDUI_FONT_SMALL, g_edui.tip, box, ED_ALIGN_CENTRE, EDC_TEXT);
}

void EdUI_EndFrame(void)
{
    s_overlayPass = 1;
    g_edui.clipTop = 0;                 // the overlays are clipped by nothing
    edui_draw_menu();
    edui_draw_combo();
    edui_draw_tip();
    s_overlayPass = 0;

    if (edui_overlay_open()) g_edui.wantsMouse = 1;
    if (g_edui.active != 0)  g_edui.wantsMouse = 1;
}
