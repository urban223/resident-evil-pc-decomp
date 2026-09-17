// EditorUITheme.h - the editor's colours and metrics, in one place.
//
// CUSTOM (port-only).
//
// Every number the interface is built from lives here rather than at its use
// site, for the same reason a stylesheet exists: a panel that picks its own
// grey is a panel that stops matching the one beside it the first time either
// is touched.
//
// Colours are 0xAARRGGBB, which is what MarniDrawSpriteEx takes.
//
// Lengths are DESIGN pixels: the interface is laid out as though the window
// were 1920x1080 and scaled from there (EdUI_Scale), so a row is 22 units tall
// whether the window is 720p or 4K.
#pragma once

// --- surfaces ---------------------------------------------------------------
#define EDC_WINDOW      0xFF141414u   // behind everything; the letterbox too
#define EDC_PANEL       0xFF222427u   // a dock's body
#define EDC_PANEL_ALT   0xFF1B1D20u   // the darker body a list sits in
#define EDC_HEADER      0xFF2B2E32u   // a dock's title strip
#define EDC_TOOLBAR     0xFF26292Du
#define EDC_MENUBAR     0xFF1B1D20u
#define EDC_STATUS      0xFF1B1D20u
#define EDC_POPUP       0xFF2A2D31u
#define EDC_VIEWBG      0xFF0C0D0Fu   // what shows around the rendered frame

// --- lines ------------------------------------------------------------------
#define EDC_BORDER      0xFF101113u   // between docks: darker than both
#define EDC_BORDER_SOFT 0x30FFFFFFu   // a hairline inside a panel
#define EDC_SEP         0xFF35383Du

// --- text -------------------------------------------------------------------
#define EDC_TEXT        0xFFE2E4E7u
#define EDC_TEXT_DIM    0xFF9AA0A6u
#define EDC_TEXT_FAINT  0xFF6B7076u
#define EDC_TEXT_ON     0xFFFFFFFFu   // on an accent fill

// --- interaction ------------------------------------------------------------
#define EDC_HOVER       0x1AFFFFFFu   // an overlay, not a colour: works on any bed
#define EDC_PRESS       0x33000000u
#define EDC_ACCENT      0xFF2E8FE0u   // the one blue the interface uses
#define EDC_ACCENT_DIM  0xFF1F5F96u
#define EDC_SELECT      0xFF2B4F73u   // a selected row's bed
#define EDC_SELECT_SOFT 0x552E8FE0u
#define EDC_FIELD       0xFF15171Au   // an editable box
#define EDC_FIELD_LINE  0xFF3A3E44u

// --- meaning ----------------------------------------------------------------
#define EDC_X           0xFFE0564Eu   // the axis colours, shared with the gizmo
#define EDC_Y           0xFF7FD06Au
#define EDC_Z           0xFF5D8FD6u
#define EDC_PLAY        0xFF4CC46Au
#define EDC_WARN        0xFFE0B23Cu
#define EDC_BAD         0xFFE0564Eu

// --- metrics (design px) ----------------------------------------------------
#define EDM_MENUBAR_H   24.0f
#define EDM_TOOLBAR_H   44.0f
#define EDM_STATUS_H    22.0f
#define EDM_HEADER_H    26.0f
#define EDM_ROW_H       22.0f
#define EDM_FIELD_H     20.0f
#define EDM_PAD         8.0f
#define EDM_GAP         4.0f
#define EDM_SPLITTER    4.0f
#define EDM_SCROLL_W    10.0f
#define EDM_INDENT      14.0f
#define EDM_ICON        16.0f
#define EDM_TOOL_ICON   20.0f
#define EDM_RADIUS_S    3.0f          // matches EDUI_TILE_ROUND3
#define EDM_RADIUS_M    5.0f
