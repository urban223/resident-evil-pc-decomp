// EditorContent.h - what the editor can place, and what it calls it.
//
// CUSTOM (port-only). See EditorContent.cpp.
#pragma once

struct EdItemDef {
    int         type;        // an ITEM_* id
    const char* label;       // NULL for the game's own name, else a format
                             // for it - see EditorContent.cpp
    const char* model;       // the .ivm the pickup is drawn with, "" for none
};

extern const EdItemDef g_edItems[];
extern const int        g_edItemCount;
extern const char* const g_edEnemyNames[];
extern const int         g_edEnemyCount;

const char* EdContent_ItemName(int type);
const char* EdContent_ItemNameAt(int index);   // by g_edItems[] row
const char* EdContent_EnemyName(int type);
int         EdContent_ItemStacks(int type);
