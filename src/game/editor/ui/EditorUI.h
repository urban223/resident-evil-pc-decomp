// EditorUI.h - the editor's own widget toolkit.
//
// CUSTOM (port-only). Nothing here is in the original game.
//
// WHY THERE IS A TOOLKIT AT ALL
//
// The editor is the game: one process, one renderer, one window. The viewport
// is not a picture of the game sent somewhere else, it is the game drawn into
// part of the window, and the panels around it are drawn by the same renderer
// in the same frame. That is how Unreal's editor is built - Slate is Unreal's
// own C++ widget toolkit and the editor's renderer draws it - and it is the
// only arrangement in which "what you see is what runs" is literally true.
//
// It does mean the interface has to be built from what the renderer can do,
// which is exactly one thing: a textured, tinted quad. So there is a baked
// atlas (tools/build_editor_ui.py) holding the fonts, the rounded plates and
// the icons, and there is this: the layer that turns quads into widgets.
//
// IMMEDIATE MODE
//
// A widget is a function call that draws and answers in the same breath. There
// is no widget tree to keep in sync with the level, which matters here more
// than usual: the thing being edited is a handful of C arrays that a reload
// replaces wholesale, and any retained tree pointing into them would be stale
// the moment F7 is pressed. The only state the toolkit keeps between frames is
// interaction state - what is hovered, what is being dragged, where a list is
// scrolled - keyed by an id, never by a pointer.
//
// UNITS
//
// Everything in this header is in DESIGN pixels: the interface is laid out as
// though the window were 1920x1080, and EdUI_Scale() maps that to the real
// backbuffer at the last moment. A row is 22 units tall at every resolution;
// at 4K it simply covers twice as many real pixels, and the text is baked at
// twice its design size so it stays sharp when it does.
#pragma once

#include "EditorUIData.h"
#include "EditorUITheme.h"

// ---------------------------------------------------------------------------
// Rectangles. Design pixels, top-left origin, Y down.
// ---------------------------------------------------------------------------
struct EdRect {
    float x, y, w, h;
};

EdRect  EdR(float x, float y, float w, float h);
EdRect  EdR_Inset(EdRect r, float by);
EdRect  EdR_Cut(EdRect* r, float amount, int side);   // carve a strip off r
int     EdR_Has(EdRect r, float x, float y);

#define ED_SIDE_TOP     0
#define ED_SIDE_BOTTOM  1
#define ED_SIDE_LEFT    2
#define ED_SIDE_RIGHT   3

// ---------------------------------------------------------------------------
// The frame.
//
// EdUI_BeginFrame samples the mouse and the window size and clears the
// per-frame interaction state; EdUI_EndFrame draws the deferred layers - the
// open menu, a combo's list, the tooltip - which is why they land on top of
// everything without the panels needing to know they exist.
// ---------------------------------------------------------------------------
int   EdUI_Ready(void);           // the atlas is loaded; nothing draws before
void  EdUI_BeginFrame(void);
void  EdUI_EndFrame(void);

float EdUI_Scale(void);           // design px -> backbuffer px
float EdUI_Width(void);           // the window, in design px
float EdUI_Height(void);
float EdUI_MouseX(void);
float EdUI_MouseY(void);

// 1 while the pointer is over the interface rather than the viewport, so the
// camera and the picking ray can stay out of the way of a panel.
int   EdUI_WantsMouse(void);
// 1 while a text field has the keyboard, so the fly keys do not type.
int   EdUI_WantsKeyboard(void);

// ---------------------------------------------------------------------------
// Ids.
//
// A widget's identity is a hash of a string and an index, never a pointer -
// see the note at the top about reloads.
// ---------------------------------------------------------------------------
typedef unsigned int EdId;
EdId EdUI_Id(const char* key);
EdId EdUI_IdIdx(const char* key, int index);

// ---------------------------------------------------------------------------
// Drawing. Clipped to the current clip rectangle, which panels push and pop.
// ---------------------------------------------------------------------------
void  EdUI_PushClip(EdRect r);
void  EdUI_PopClip(void);

void  EdUI_Fill(EdRect r, unsigned int argb);
void  EdUI_Plate(EdRect r, int tile, unsigned int argb);    // 9-slice, filled
void  EdUI_Outline(EdRect r, unsigned int argb);            // 1px, square
void  EdUI_Icon(EdRect r, int icon, unsigned int argb);
void  EdUI_Line(float x0, float y0, float x1, float y1, float w,
                unsigned int argb);

#define ED_ALIGN_LEFT    0
#define ED_ALIGN_CENTRE  1
#define ED_ALIGN_RIGHT   2

float EdUI_TextW(int font, const char* s);
float EdUI_FontH(int font);
void  EdUI_Text(int font, const char* s, float x, float y, unsigned int argb);
// Inside `box`, vertically centred, ellipsised if it does not fit.
void  EdUI_TextIn(int font, const char* s, EdRect box, int align,
                  unsigned int argb);

// ---------------------------------------------------------------------------
// Widgets. Each returns non-zero on the frame it was used.
// ---------------------------------------------------------------------------
#define ED_BTN_FLAT      0x01     // no plate until hovered (toolbar, menu row)
#define ED_BTN_ACCENT    0x02     // the one primary action on a bar
#define ED_BTN_ON        0x04     // a toggle that is on
#define ED_BTN_DISABLED  0x08
#define ED_BTN_ICON_ONLY 0x10

// A toggle's state reads better at the call site as a condition than as an
// if/else that picks between two flag words.
#define ED_BTN_IF_ON(c)  ((c) ? ED_BTN_ON : 0)

int   EdUI_Button(EdId id, EdRect r, const char* label, int icon, int flags);

// Press/release bookkeeping for a panel drawing its own shape rather than a
// standard widget: returns 1 on a completed click, and reports whether the
// button is still down on it.
int   EdUI_Clickable(EdId id, EdRect r, int* outHeld);

// Is this widget the one under the cursor? Panels ask so they can put up a
// tooltip without reaching into the toolkit's state.
int   EdUI_IsHot(EdId id);
int   EdUI_Check(EdId id, EdRect r, const char* label, int* value);
int   EdUI_DragInt(EdId id, EdRect r, int* value, float unitsPerPixel,
                   int lo, int hi, const char* suffix);
int   EdUI_SliderFloat(EdId id, EdRect r, float* value, float lo, float hi,
                       const char* fmt);
int   EdUI_TextField(EdId id, EdRect r, char* buf, int cap);
int   EdUI_Combo(EdId id, EdRect r, const char* const* items, int count,
                 int* index);

// A row in a list or a tree. `depth` indents, `expand` is NULL for a leaf and
// otherwise points at the open flag the twisty toggles. Returns 1 when the row
// was clicked (the twisty handles itself and does not report a click).
int   EdUI_Row(EdId id, EdRect r, int depth, int icon, const char* label,
               const char* suffix, int selected, int* expand);

// A labelled field line in the Details panel: "Position X [ 1200 ]".
EdRect EdUI_FieldRow(EdRect* body, const char* label, float labelW);

// Section header with a twisty, the way Details groups its categories.
int   EdUI_Section(EdId id, EdRect* body, const char* label, int* open);

void  EdUI_Separator(EdRect* body);
void  EdUI_Tooltip(const char* text);

// ---------------------------------------------------------------------------
// Scrolling regions.
//
// Between Begin and End the caller lays out at `contentH` design pixels of
// height starting from the returned rect's top; the region clips, offsets and
// draws the bar.
// ---------------------------------------------------------------------------
EdRect EdUI_ScrollBegin(EdId id, EdRect r, float contentH);
void   EdUI_ScrollEnd(void);

// ---------------------------------------------------------------------------
// Splitters. `value` is the size in design px of the pane on the low side and
// is clamped between lo and hi.
// ---------------------------------------------------------------------------
int   EdUI_SplitterV(EdId id, EdRect r, float* value, float lo, float hi);
int   EdUI_SplitterH(EdId id, EdRect r, float* value, float lo, float hi);

// ---------------------------------------------------------------------------
// The menu bar. `menus` is a NULL-terminated list of titles; the callback is
// asked to emit the open menu's items, which it does by calling EdUI_MenuItem.
// ---------------------------------------------------------------------------
void  EdUI_MenuBarBegin(EdRect r);
int   EdUI_MenuBegin(const char* title);   // 1 while this menu is open
int   EdUI_MenuItem(const char* label, const char* shortcut, int icon,
                    int enabled);
void  EdUI_MenuCheck(const char* label, const char* shortcut, int* value);
void  EdUI_MenuSeparator(void);
void  EdUI_MenuEnd(void);
void  EdUI_MenuBarEnd(void);

// ---------------------------------------------------------------------------
// The atlas, for the panels that want to draw something of their own.
// ---------------------------------------------------------------------------
void  EdUI_Quad(const EdUiRect* src, EdRect dst, unsigned int argb);
