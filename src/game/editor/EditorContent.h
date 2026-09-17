// EditorContent.h - what the editor can place, and what it calls it.
//
// CUSTOM (port-only). See EditorContent.cpp.
#pragma once

struct EdItemDef {
    int         type;        // an ITEM_* id
    const char* name;        // the game's own name for it
    const char* model;       // the .ivm the pickup is drawn with, "" for none
};

extern const EdItemDef g_edItems[];
extern const int        g_edItemCount;
extern const char* const g_edEnemyNames[];
extern const int         g_edEnemyCount;

const char* EdContent_ItemName(int type);
const char* EdContent_EnemyName(int type);
int         EdContent_ItemStacks(int type);
