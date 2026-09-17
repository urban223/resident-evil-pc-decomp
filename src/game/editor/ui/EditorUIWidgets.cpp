// EditorUIWidgets.cpp - buttons, fields, rows, the things panels are made of.
//
// CUSTOM (port-only).
//
// Every widget here is the same three steps in a different order: ask the core
// whether the cursor is on it, draw the state that answer implies, and report
// what the user did. Nothing keeps a record of itself between frames except
// through the core's hot/active/focus ids.
//
// The numeric field deserves a note. Unreal's is a drag AND a text box - you
// grab it and pull to scrub, or you click and type an exact number - and both
// halves matter here, because "somewhere around there" is how you place a wall
// and "exactly 6000" is how you finish one. So the same rectangle is both: a
// press that moves scrubs, a press that does not opens the caret.
#include "EditorUIInternal.h"
#include "../../../platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
int EdUI_Button(EdId id, EdRect r, const char* label, int icon, int flags)
{
    const int disabled = (flags & ED_BTN_DISABLED) != 0;
    int held = 0;
    int clicked = 0;
    int over = 0;

    if (!disabled) {
        clicked = EdUiCore_Clicked(id, r, &held);
        over = (g_edui.hot == id) || held;
    }

    unsigned int bed = 0;
    if (flags & ED_BTN_ACCENT)   bed = EDC_ACCENT;
    else if (flags & ED_BTN_ON)  bed = EDC_ACCENT_DIM;
    else if (!(flags & ED_BTN_FLAT)) bed = 0xFF34383Du;

    if (bed) EdUI_Plate(r, EDUI_TILE_ROUND3, bed);
    if (over && !disabled) {
        EdUI_Plate(r, EDUI_TILE_ROUND3, held ? EDC_PRESS : EDC_HOVER);
    }
    if ((flags & ED_BTN_ON) && !(flags & ED_BTN_ACCENT)) {
        // A pressed-in toggle gets a lit underline as well as its bed: on a
        // toolbar of icon-only buttons the bed alone is too quiet to scan.
        EdUI_Fill(EdR(r.x + 3.0f, r.y + r.h - 2.0f, r.w - 6.0f, 2.0f), EDC_ACCENT);
    }

    unsigned int col = disabled ? EDC_TEXT_FAINT
                     : ((flags & ED_BTN_ACCENT) ? EDC_TEXT_ON : EDC_TEXT);

    const int iconOnly = (flags & ED_BTN_ICON_ONLY) || label == NULL || !*label;
    if (icon >= 0 && iconOnly) {
        EdUI_Icon(EdR(r.x, r.y, r.w, r.h), icon, col);
    } else if (icon >= 0) {
        const float is = EDM_ICON;
        EdUI_Icon(EdR(r.x + EDM_GAP + 1.0f, r.y + (r.h - is) * 0.5f, is, is),
                  icon, col);
        EdUI_TextIn(EDUI_FONT_UI, label,
                    EdR(r.x + is + EDM_GAP * 2.0f, r.y,
                        r.w - is - EDM_GAP * 3.0f, r.h), ED_ALIGN_LEFT, col);
    } else {
        EdUI_TextIn(EDUI_FONT_UI, label, r, ED_ALIGN_CENTRE, col);
    }
    return clicked;
}

int EdUI_Check(EdId id, EdRect r, const char* label, int* value)
{
    int held = 0;
    const int clicked = EdUiCore_Clicked(id, r, &held);
    const int over = (g_edui.hot == id) || held;
    if (clicked && value) *value = !*value;

    const float b = 14.0f;
    const EdRect box = EdR(r.x, r.y + (r.h - b) * 0.5f, b, b);
    const int on = value && *value;

    EdUI_Plate(box, EDUI_TILE_ROUND3, on ? EDC_ACCENT : EDC_FIELD);
    EdUI_Plate(box, EDUI_TILE_LINE3, over ? EDC_ACCENT : EDC_FIELD_LINE);
    if (on) EdUI_Icon(EdR_Inset(box, 1.0f), EDUI_ICON_CHECK, EDC_TEXT_ON);

    if (label && *label) {
        EdUI_TextIn(EDUI_FONT_UI, label,
                    EdR(r.x + b + 6.0f, r.y, r.w - b - 6.0f, r.h),
                    ED_ALIGN_LEFT, EDC_TEXT);
    }
    return clicked;
}

// ---------------------------------------------------------------------------
// Text editing.
//
// One caret in the whole interface (g_edui.focus), so one buffer. A field
// takes the caret by being clicked, keeps it until something else is clicked
// or Enter/Escape ends it, and writes back only on a commit - which is what
// lets a half-typed number be nonsense for a moment without the level jumping.
// ---------------------------------------------------------------------------
static void edit_begin(EdId id, const char* text)
{
    g_edui.focus = id;
    strncpy(g_edui.editBuf, text ? text : "", EDUI_TEXT_CAP - 1);
    g_edui.editBuf[EDUI_TEXT_CAP - 1] = '\0';
    g_edui.editLen = (int)strlen(g_edui.editBuf);
    g_edui.editCaret = g_edui.editLen;
    g_edui.editBlink = 0;
}

#define EDIT_NONE    0
#define EDIT_TYPING  1
#define EDIT_COMMIT  2
#define EDIT_CANCEL  3

static int edit_update(int numericOnly)
{
    int ch;
    while ((ch = EdUiKey_Char()) != 0) {
        if (ch == '\r' || ch == '\n') return EDIT_COMMIT;
        if (ch == 27) return EDIT_CANCEL;
        if (ch == '\b') {
            if (g_edui.editCaret > 0) {
                memmove(g_edui.editBuf + g_edui.editCaret - 1,
                        g_edui.editBuf + g_edui.editCaret,
                        (size_t)(g_edui.editLen - g_edui.editCaret + 1));
                g_edui.editCaret--;
                g_edui.editLen--;
            }
            continue;
        }
        if (ch == '\t') return EDIT_COMMIT;
        if (ch < 32 || ch > 126) continue;
        if (numericOnly && !((ch >= '0' && ch <= '9') || ch == '-')) continue;
        if (g_edui.editLen >= EDUI_TEXT_CAP - 1) continue;
        memmove(g_edui.editBuf + g_edui.editCaret + 1,
                g_edui.editBuf + g_edui.editCaret,
                (size_t)(g_edui.editLen - g_edui.editCaret + 1));
        g_edui.editBuf[g_edui.editCaret] = (char)ch;
        g_edui.editCaret++;
        g_edui.editLen++;
        g_edui.editBlink = 0;
    }

    if (EdUiKey_Pressed(VK_LEFT)  && g_edui.editCaret > 0) g_edui.editCaret--;
    if (EdUiKey_Pressed(VK_RIGHT) && g_edui.editCaret < g_edui.editLen) g_edui.editCaret++;
    if (EdUiKey_Pressed(VK_HOME))  g_edui.editCaret = 0;
    if (EdUiKey_Pressed(VK_END))   g_edui.editCaret = g_edui.editLen;
    if (EdUiKey_Pressed(VK_DELETE) && g_edui.editCaret < g_edui.editLen) {
        memmove(g_edui.editBuf + g_edui.editCaret,
                g_edui.editBuf + g_edui.editCaret + 1,
                (size_t)(g_edui.editLen - g_edui.editCaret));
        g_edui.editLen--;
    }
    if (EdUiKey_Pressed(VK_RETURN)) return EDIT_COMMIT;
    if (EdUiKey_Pressed(VK_ESCAPE)) return EDIT_CANCEL;
    return EDIT_TYPING;
}

static void edit_draw(EdRect r, int font)
{
    const float pad = 5.0f;
    EdUI_TextIn(font, g_edui.editBuf, EdR(r.x + pad, r.y, r.w - pad * 2.0f, r.h),
                ED_ALIGN_LEFT, EDC_TEXT);

    if (((g_edui.editBlink / 20) & 1) == 0) {
        char head[EDUI_TEXT_CAP];
        memcpy(head, g_edui.editBuf, (size_t)g_edui.editCaret);
        head[g_edui.editCaret] = '\0';
        const float cx = r.x + pad + EdUI_TextW(font, head);
        const float ch = EdUI_FontH(font);
        EdUI_Fill(EdR(cx, r.y + (r.h - ch) * 0.5f, 1.0f, ch), EDC_TEXT);
    }
}

int EdUI_TextField(EdId id, EdRect r, char* buf, int cap)
{
    const int editing = (g_edui.focus == id);
    int over = EdUiCore_Hover(id, r);
    int done = 0;

    if (over && g_edui.mpressed[0] && !editing) {
        edit_begin(id, buf);
        g_edui.active = 0;
    } else if (!over && g_edui.mpressed[0] && editing) {
        // Clicking away commits, which is what every other editor does and
        // what makes tabbing through a details panel feel right.
        strncpy(buf, g_edui.editBuf, (size_t)cap - 1);
        buf[cap - 1] = '\0';
        g_edui.focus = 0;
        done = 1;
    }

    EdUI_Plate(r, EDUI_TILE_ROUND3, EDC_FIELD);
    EdUI_Plate(r, EDUI_TILE_LINE3,
               editing ? EDC_ACCENT : (over ? 0xFF55595Fu : EDC_FIELD_LINE));

    if (editing) {
        const int st = edit_update(0);
        if (st == EDIT_COMMIT) {
            strncpy(buf, g_edui.editBuf, (size_t)cap - 1);
            buf[cap - 1] = '\0';
            g_edui.focus = 0;
            done = 1;
        } else if (st == EDIT_CANCEL) {
            g_edui.focus = 0;
        } else {
            edit_draw(r, EDUI_FONT_UI);
        }
    }
    if (!editing || g_edui.focus != id) {
        EdUI_TextIn(EDUI_FONT_UI, buf, EdR(r.x + 5.0f, r.y, r.w - 10.0f, r.h),
                    ED_ALIGN_LEFT, EDC_TEXT);
    }
    return done;
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------
int EdUI_DragInt(EdId id, EdRect r, int* value, float unitsPerPixel,
                 int lo, int hi, const char* suffix)
{
    if (value == NULL) return 0;
    const int editing = (g_edui.focus == id);
    int changed = 0;
    int over = editing ? 0 : EdUiCore_Hover(id, r);
    const int held = (g_edui.active == id);

    if (over && g_edui.mpressed[0]) {
        g_edui.active = id;
        g_edui.dragX = g_edui.mx;
        g_edui.dragY = g_edui.my;
        g_edui.dragA = (float)*value;
        g_edui.dragB = 0.0f;                // how far it has actually moved
    }
    if (held) {
        const float dx = g_edui.mx - g_edui.dragX;
        if (dx != 0.0f) g_edui.dragB = 1.0f;
        // Shift is the fine modifier everywhere else in this editor; it is the
        // fine modifier here too.
        const float k = EdUiKey_Held(VK_SHIFT) ? unitsPerPixel * 0.1f
                                               : unitsPerPixel;
        int v = (int)(g_edui.dragA + dx * k);
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        if (v != *value) { *value = v; changed = 1; }
    }
    if (g_edui.active == id && g_edui.mreleased[0]) {
        g_edui.active = 0;
        if (g_edui.dragB == 0.0f) {         // a press that never moved: type
            char t[32];
            snprintf(t, sizeof(t), "%d", *value);
            edit_begin(id, t);
        }
    }

    EdUI_Plate(r, EDUI_TILE_ROUND3, EDC_FIELD);
    EdUI_Plate(r, EDUI_TILE_LINE3,
               editing ? EDC_ACCENT : ((over || held) ? 0xFF55595Fu : EDC_FIELD_LINE));

    if (editing) {
        const int st = edit_update(1);
        if (st == EDIT_COMMIT) {
            int v = atoi(g_edui.editBuf);
            if (v < lo) v = lo;
            if (v > hi) v = hi;
            if (v != *value) { *value = v; changed = 1; }
            g_edui.focus = 0;
        } else if (st == EDIT_CANCEL) {
            g_edui.focus = 0;
        } else {
            edit_draw(r, EDUI_FONT_UI);
        }
    }
    if (g_edui.focus != id) {
        char t[48];
        if (suffix && *suffix) snprintf(t, sizeof(t), "%d%s", *value, suffix);
        else                   snprintf(t, sizeof(t), "%d", *value);
        EdUI_TextIn(EDUI_FONT_UI, t, EdR(r.x + 6.0f, r.y, r.w - 12.0f, r.h),
                    ED_ALIGN_LEFT, EDC_TEXT);
        if (over || held) {
            // The two chevrons say "this scrubs" without adding a control.
            EdUI_Icon(EdR(r.x + r.w - 14.0f, r.y + 1.0f, 12.0f, r.h - 2.0f),
                      EDUI_ICON_CHEVRON_R, 0x55FFFFFFu);
        }
    }
    return changed;
}

int EdUI_SliderFloat(EdId id, EdRect r, float* value, float lo, float hi,
                     const char* fmt)
{
    if (value == NULL || hi <= lo) return 0;
    int held = 0;
    const int over = EdUiCore_Hover(id, r);
    if (over && g_edui.mpressed[0]) g_edui.active = id;
    if (g_edui.active == id) {
        held = 1;
        float t = (g_edui.mx - r.x - 6.0f) / (r.w - 12.0f);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        *value = lo + (hi - lo) * t;
    }

    EdUI_Plate(r, EDUI_TILE_ROUND3, EDC_FIELD);
    float t = (*value - lo) / (hi - lo);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    EdUI_Plate(EdR(r.x, r.y, 6.0f + (r.w - 12.0f) * t + 6.0f, r.h),
               EDUI_TILE_ROUND3, (held || over) ? EDC_ACCENT_DIM : 0xFF2C4A63u);
    EdUI_Plate(r, EDUI_TILE_LINE3, held ? EDC_ACCENT : EDC_FIELD_LINE);

    char txt[48];
    snprintf(txt, sizeof(txt), fmt ? fmt : "%.2f", (double)*value);
    EdUI_TextIn(EDUI_FONT_SMALL, txt, r, ED_ALIGN_CENTRE, EDC_TEXT);
    return held;
}

// ---------------------------------------------------------------------------
// Combo
// ---------------------------------------------------------------------------
int EdUI_Combo(EdId id, EdRect r, const char* const* items, int count,
               int* index)
{
    int changed = 0;
    if (g_edui.comboPicked >= 0 && g_edui.comboPickedId == id && index) {
        if (*index != g_edui.comboPicked) {
            *index = g_edui.comboPicked;
            changed = 1;
        }
        g_edui.comboPicked = -1;
    }

    int held = 0;
    const int clicked = EdUiCore_Clicked(id, r, &held);
    const int over = (g_edui.hot == id) || held;
    const int open = (g_edui.comboOpen == id);

    if (clicked) {
        if (open) {
            g_edui.comboOpen = 0;
        } else {
            g_edui.comboOpen = id;
            g_edui.comboPickedId = id;
            g_edui.comboRect = r;
            g_edui.comboItems = items;
            g_edui.comboCount = count;
        }
    } else if (open) {
        // Keep the list fed: the caller may have changed what is in it.
        g_edui.comboRect = r;
        g_edui.comboItems = items;
        g_edui.comboCount = count;
    }

    EdUI_Plate(r, EDUI_TILE_ROUND3, EDC_FIELD);
    EdUI_Plate(r, EDUI_TILE_LINE3,
               open ? EDC_ACCENT : (over ? 0xFF55595Fu : EDC_FIELD_LINE));

    const int i = index ? *index : 0;
    const char* label = (items && i >= 0 && i < count) ? items[i] : "";
    EdUI_TextIn(EDUI_FONT_UI, label,
                EdR(r.x + 6.0f, r.y, r.w - 24.0f, r.h), ED_ALIGN_LEFT, EDC_TEXT);
    EdUI_Icon(EdR(r.x + r.w - 18.0f, r.y, 14.0f, r.h),
              EDUI_ICON_CHEVRON_D, EDC_TEXT_DIM);
    return changed;
}

// ---------------------------------------------------------------------------
// Rows, sections, field lines
// ---------------------------------------------------------------------------
int EdUI_Row(EdId id, EdRect r, int depth, int icon, const char* label,
             const char* suffix, int selected, int* expand)
{
    int held = 0;
    int clicked = EdUiCore_Clicked(id, r, &held);
    const int over = (g_edui.hot == id) || held;

    if (selected)   EdUI_Fill(r, EDC_SELECT);
    else if (over)  EdUI_Fill(r, EDC_HOVER);

    float x = r.x + EDM_GAP + depth * EDM_INDENT;

    if (expand) {
        const EdRect tw = EdR(x, r.y, 14.0f, r.h);
        const EdId tid = id ^ 0x71575u;
        if (EdUiCore_Clicked(tid, tw, NULL)) { *expand = !*expand; clicked = 0; }
        EdUI_Icon(EdR(x, r.y, 14.0f, r.h),
                  *expand ? EDUI_ICON_CHEVRON_D : EDUI_ICON_CHEVRON_R,
                  EDC_TEXT_DIM);
        x += 16.0f;
    }

    if (icon >= 0) {
        EdUI_Icon(EdR(x, r.y + (r.h - EDM_ICON) * 0.5f, EDM_ICON, EDM_ICON),
                  icon, selected ? EDC_TEXT : EDC_TEXT_DIM);
        x += EDM_ICON + 5.0f;
    }

    float rightW = 0.0f;
    if (suffix && *suffix) {
        rightW = EdUI_TextW(EDUI_FONT_SMALL, suffix) + EDM_PAD;
        EdUI_TextIn(EDUI_FONT_SMALL, suffix,
                    EdR(r.x, r.y, r.w - EDM_PAD, r.h), ED_ALIGN_RIGHT,
                    EDC_TEXT_FAINT);
    }
    EdUI_TextIn(EDUI_FONT_UI, label,
                EdR(x, r.y, r.x + r.w - x - rightW - EDM_GAP, r.h),
                ED_ALIGN_LEFT, selected ? EDC_TEXT_ON : EDC_TEXT);
    return clicked;
}

int EdUI_Section(EdId id, EdRect* body, const char* label, int* open)
{
    const EdRect r = EdR_Cut(body, EDM_HEADER_H, ED_SIDE_TOP);
    int held = 0;
    const int clicked = EdUiCore_Clicked(id, r, &held);
    if (clicked && open) *open = !*open;
    const int over = (g_edui.hot == id) || held;

    EdUI_Fill(r, over ? 0xFF33373Cu : 0xFF2A2D31u);
    EdUI_Fill(EdR(r.x, r.y + r.h - 1.0f, r.w, 1.0f), EDC_BORDER);
    EdUI_Icon(EdR(r.x + EDM_GAP, r.y, 14.0f, r.h),
              (open && *open) ? EDUI_ICON_CHEVRON_D : EDUI_ICON_CHEVRON_R,
              EDC_TEXT_DIM);
    EdUI_TextIn(EDUI_FONT_BOLD, label,
                EdR(r.x + EDM_GAP + 18.0f, r.y, r.w - 30.0f, r.h),
                ED_ALIGN_LEFT, EDC_TEXT);
    return open ? *open : 1;
}

EdRect EdUI_FieldRow(EdRect* body, const char* label, float labelW)
{
    EdRect row = EdR_Cut(body, EDM_ROW_H + 2.0f, ED_SIDE_TOP);
    row = EdR_Inset(row, 1.0f);
    row.x += EDM_PAD;
    row.w -= EDM_PAD * 2.0f;
    if (label && *label) {
        EdUI_TextIn(EDUI_FONT_UI, label, EdR(row.x, row.y, labelW, row.h),
                    ED_ALIGN_LEFT, EDC_TEXT_DIM);
    }
    return EdR(row.x + labelW, row.y, row.w - labelW, row.h);
}

void EdUI_Separator(EdRect* body)
{
    const EdRect r = EdR_Cut(body, 7.0f, ED_SIDE_TOP);
    EdUI_Fill(EdR(r.x + EDM_PAD, r.y + 3.0f, r.w - EDM_PAD * 2.0f, 1.0f), EDC_SEP);
}
