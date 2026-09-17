// EditorContent.cpp - GENERATED IN PART: the names the editor calls things by.
//
// CUSTOM (port-only).
//
// The engine knows items by number. A content browser that offers "type 11"
// is a hex editor with a mouse, so the names live here - the game's own,
// transcribed from the item tables, and the enemy list in the order
// cmd_enemy_set takes.
//
// This is also where "what can be placed" is decided: the palette and the
// content browser both read these tables, so adding an item type to the
// editor is one line here rather than a hunt through the panels.
#include "EditorContent.h"
#include "../RaidLevel.h"
#include "../Types.h"

const EdItemDef g_edItems[] = {
    {   1, "COMBAT KNIFE", "i05v" },
    {   2, "BERETTA", "i00v" },
    {   3, "SHOTGUN", "i02v" },
    {   4, "COLT PYTHON (DUM-DUM)", "i04v" },
    {   5, "COLT PYTHON (MAGNUM)", "i04v" },
    {   6, "FLAMETHROWER", "i01v" },
    {   7, "BAZOOKA (EXPLOSIVE)", "i08v" },
    {   8, "BAZOOKA (ACID)", "i08v" },
    {   9, "BAZOOKA (FLAME)", "i08v" },
    {  10, "R. LAUNCHER", "i07v" },
    {  11, "CLIP", "i26v" },
    {  12, "SHELLS", "i16v" },
    {  13, "DUMDUM ROUNDS", "i06v" },
    {  14, "MAGNUM ROUNDS", "i15v" },
    {  15, "FUEL", "i11v" },
    {  16, "EXPLOSIVE R.", "i14v" },
    {  17, "ACID ROUNDS", "i12v" },
    {  18, "FLAME ROUNDS", "i13v" },
    {  19, "EMPTY BOTTLE", "i50v" },
    {  20, "WATER", "i21v" },
    {  21, "UMB No.2", "i22v" },
    {  22, "UMB No.4", "i48v" },
    {  23, "UMB No.7", "i58v" },
    {  24, "UMB No.13", "i47v" },
    {  25, "Yellow-6", "i49v" },
    {  26, "NP-003", "i62v" },
    {  27, "V-JOLT", "i09v" },
    {  28, "BROKEN SHOTGUN", "i03v" },
    {  29, "SQUARE CRANK", "i19v" },
    {  30, "HEX. CRANK", "i72v" },
    {  31, "EMBLEM", "i38v" },
    {  32, "GOLD EMBLEM", "i39v" },
    {  33, "BLUE JEWEL", "i41v" },
    {  34, "RED JEWEL", "i42v" },
    {  35, "MUSIC NOTES", "i45v" },
    {  36, "WOLF MEDAL", "i24v" },
    {  37, "EAGLE MEDAL", "i25v" },
    {  38, "HERBICIDE", "i43v" },
    {  39, "BATTERY", "i54v" },
    {  40, "MO DISK", "i37v" },
    {  41, "WIND CREST", "i44v" },
    {  42, "FLARE", "i55v" },
    {  43, "SLIDES", "i46v" },
    {  44, "MOON CREST", "i60v" },
    {  45, "STAR CREST", "i61v" },
    {  46, "SUN CREST", "i59v" },
    {  47, "INK RIBBON", "i56v" },
    {  48, "LIGHTER", "i17v" },
    {  49, "LOCKPICK", "i18v" },
    {  50, "OIL", "i40v" },
    {  51, "SWORD KEY", "i30v" },
    {  52, "ARMOR KEY", "i28v" },
    {  53, "SHIELD KEY", "i29v" },
    {  54, "HELMET KEY", "i27v" },
    {  55, "MASTER KEY", "i34v" },
    {  56, "CLOSET KEY", "i73v" },
    {  57, "002 KEY", "i32v" },
    {  58, "003 KEY", "i31v" },
    {  59, "C. ROOM KEY", "i36v" },
    {  60, "P. ROOM KEY", "i33v" },
    {  61, "DESK KEY", "i63v" },
    {  62, "BLANK BOOK", "i53v" },
    {  63, "DOOM BOOK 2", "i51v" },
    {  64, "DOOM BOOK 1", "i52v" },
    {  65, "F.-AID SPRAY", "i23v" },
    {  66, "SERUM", "i20v" },
    {  67, "RED HERB", "i66v" },
    {  68, "GREEN HERB", "i64v" },
    {  69, "BLUE HERB", "i65v" },
    {  70, "MIXED HERBS (BLUE+RED)", "i69v" },
    {  71, "MIXED HERBS (2 GREEN)", "i67v" },
    {  72, "MIXED HERBS (GREEN+BLUE)", "i68v" },
    {  73, "MIXED HERBS (G+R+B)", "i70v" },
    {  74, "MIXED HERBS (3 GREEN)", "i71v" },
    {  75, "MIXED HERBS (2 GREEN+RED)", "i75v" },
    {  76, "PICK AXE", "i10v" },
    {  77, "COM. RADIO", "i57v" },
    {  78, "MAP MANSION 1F", "" },
    {  79, "MAP MANSION 2F", "i48v" },
    {  80, "MAP COURTYARD", "i52v" },
    {  81, "MAP UNDERGROUND", "" },
    {  82, "MAP GUARDHOUSE", "" },
    {  83, "MAP LABORATORY", "i50v" },
    { 111, "0x6f INGRAM", "ING" },
    { 112, "0x70 MINIMI", "MINI" },
    { 113, "FLARE PISTOL", "IFLR" },
    { 114, "ACID PISTOL", "IACD" },
    { 115, "FREEZE PISTOL", "IFRZ" },
};
const int g_edItemCount = (int)(sizeof(g_edItems) / sizeof(g_edItems[0]));

const char* const g_edEnemyNames[] = {
    "ZOMBIE (white coat)",
    "ZOMBIE (naked)",
    "CERBERUS",
    "WEB SPINNER",
    "BLACK TIGER",
    "CROW",
    "HUNTER",
    "WASP",
    "PLANT 42",
    "CHIMERA",
    "ADDER",
    "NEPTUNE",
    "TYRANT",
    "YAWN",
    "PLANT 42 ROOTS",
    "MONSTER PLANT",
    "TYRANT 2",
    "ZOMBIE (green coat)",
    "YAWN (second)",
    "SPIDER WEB",
    "CHRIS ARM R",
    "CHRIS ARM L",
};
const int g_edEnemyCount = (int)(sizeof(g_edEnemyNames) / sizeof(g_edEnemyNames[0]));

const char* EdContent_ItemName(int type)
{
    for (int i = 0; i < g_edItemCount; i++)
        if (g_edItems[i].type == type) return g_edItems[i].name;
    return "(unknown item)";
}

const char* EdContent_EnemyName(int type)
{
    if (type < 0 || type >= g_edEnemyCount) return "(unknown enemy)";
    return g_edEnemyNames[type];
}

// Ammunition and ink ribbons are the only things in this game that pile up in
// one slot; everything else takes a slot per unit, which is why the amount
// field is hidden for them rather than merely ignored.
int EdContent_ItemStacks(int type)
{
    return (type >= ITEM_CLIP && type <= ITEM_FLAME_ROUNDS)
        || type == ITEM_INK_RIBBONS;
}
