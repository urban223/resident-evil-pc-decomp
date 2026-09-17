// EditorUIInternal.h - what the toolkit's translation units share.
//
// CUSTOM. Nothing outside src/game/editor/ui includes this.
#pragma once
#include "EditorUI.h"

// ---------------------------------------------------------------------------
// The context.
//
// One instance, file-static in EditorUICore.cpp. Everything in it is either
// per-frame (cleared by BeginFrame) or interaction state that has to survive
// between frames because a drag does.
// ---------------------------------------------------------------------------
#define EDUI_CLIP_STACK   16
#define EDUI_SCROLL_SLOTS 32
#define EDUI_MENU_ITEMS   24
#define EDUI_TEXT_CAP     64

struct EdUiScroll {
    EdId  id;
    float offset;
    float contentH;
    float viewH;
    int   used;                 // touched this frame; stale slots get recycled
};

struct EdUiMenuItem {
    char  label[40];
    char  shortcut[16];
    int   icon;
    int   enabled;
    int   separator;
    int   check;                // -1 none, 0 off, 1 on
};

struct EdUiCtx {
    int    ready;
    float  scale;
    float  w, h;                // design px

    float  mx, my;              // design px
    float  dmx, dmy;
    int    mheld[3], mpressed[3], mreleased[3];
    float  wheel;

    EdId   hot;                 // under the cursor this frame
    EdId   hotNext;
    EdId   active;              // being dragged / typed in
    EdId   focus;               // has the keyboard
    float  dragX, dragY;        // where the drag started
    float  dragA, dragB;        // what the value was when it started

    EdRect clip[EDUI_CLIP_STACK];
    int    clipTop;

    int    wantsMouse;          // the pointer is over interface, not viewport
    EdRect viewport;            // what the viewport panel claimed

    EdUiScroll scroll[EDUI_SCROLL_SLOTS];

    // --- deferred layers, drawn by EndFrame -------------------------------
    char   tip[128];
    float  tipX, tipY;

    EdRect menuBar;
    int    menuOpen;            // index of the open menu, -1 for none
    int    menuIndex;           // the one being declared right now
    int    menuClicked;         // an item fired this frame: close afterwards
    EdRect menuAnchor;          // the title's rect, where the list drops from
    EdUiMenuItem menuItem[EDUI_MENU_ITEMS];
    int    menuCount;
    int    menuPicked;          // index of the item clicked, -1 for none
    int    menuHover;

    EdId   comboOpen;
    EdId   comboPickedId;       // whose list answered, read on the next frame
    EdRect comboRect;
    const char* const* comboItems;
    int    comboCount;
    int    comboPicked;

    // Text editing, for the one field that has the keyboard.
    char   editBuf[EDUI_TEXT_CAP];
    int    editLen;
    int    editCaret;
    int    editBlink;
};

extern EdUiCtx g_edui;

// ---------------------------------------------------------------------------
// Drawing, below the widget layer. Backbuffer pixels are computed here and
// nowhere else.
// ---------------------------------------------------------------------------
int   EdUiDraw_Ready(void);
void  EdUiDraw_Quad(const EdUiRect* src, EdRect dst, unsigned int argb);
void  EdUiDraw_Reset(void);

// The clip rectangle every quad is cut against.
EdRect EdUiDraw_Clip(void);

// Shared by the widgets: the standard hover / press bookkeeping.
int   EdUiCore_Hover(EdId id, EdRect r);
int   EdUiCore_Clicked(EdId id, EdRect r, int* outHeld);

// The keyboard, sampled once a frame by the core.
int   EdUiKey_Pressed(int vk);
int   EdUiKey_Held(int vk);
int   EdUiKey_Char(void);          // next typed character, 0 when none
