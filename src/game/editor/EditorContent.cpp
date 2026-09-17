// EditorContent.cpp - what the editor can place, and what it calls it.
//
// CUSTOM (port-only).
//
// The engine knows items by number. A content browser that offers "type 11"
// is a hex editor with a mouse, so the names are resolved here, and the enemy
// list is kept in the order cmd_enemy_set takes.
//
// This is also where "what can be placed" is decided: the palette and the
// content browser both read these tables, so adding an item type to the
// editor is one line here rather than a hunt through the panels.
//
// ITEM NAMES
//
// An item is called what the game calls it: g_ItemNamePointers, spelled in
// ASCII by ItemName_ToAscii - the decoder the status screen names items with -
// so the editor cannot drift into a name of its own. This table used to carry
// all 88 names transcribed by hand, a second copy that nothing checked.
//
// Where the game's name is not enough, the row's label is a printf format
// that is given it:
//
//   "%s (ACID)"   the game calls several items one thing (two COLT PYTHONs,
//                 three BAZOOKAs, six MIXED HERBS), and a browser has to tell
//                 them apart
//   "0x6f %s"     the two bonus weapons, which the browser has always shown
//                 with their id in front
//   "OIL"         no %s at all. ONLY for the entries the game leaves empty -
//                 OIL, the PICK AXE and the six maps are never named in a
//                 message, so their slot is s_itemEmpty. Anywhere else a
//                 full-name label is the hand copy coming back.
//
// Always the USA table, also under the Japanese assets: the editor's fonts are
// baked for ASCII 32..126 (tools/build_editor_ui.py), and a Japanese name
// would come out as a row of spaces. Japanese names in the editor start with
// baking the glyphs for them.
//
// The three custom pistols have no slot of their own in the table (see
// message_item_name_lookup in Rendering.cpp) and use the port's own strings.
#include "EditorContent.h"
#include "../RaidLevel.h"
#include "../Types.h"
#include "../../Globals.h"
#include <stdio.h>

const EdItemDef g_edItems[] = {
    {   1, NULL, "i05v" },  // COMBAT KNIFE
    {   2, NULL, "i00v" },  // BERETTA
    {   3, NULL, "i02v" },  // SHOTGUN
    {   4, "%s (DUM-DUM)", "i04v" },
    {   5, "%s (MAGNUM)", "i04v" },
    {   6, NULL, "i01v" },  // FLAMETHROWER
    {   7, "%s (EXPLOSIVE)", "i08v" },
    {   8, "%s (ACID)", "i08v" },
    {   9, "%s (FLAME)", "i08v" },
    {  10, NULL, "i07v" },  // R. LAUNCHER
    {  11, NULL, "i26v" },  // CLIP
    {  12, NULL, "i16v" },  // SHELLS
    {  13, NULL, "i06v" },  // DUMDUM ROUNDS
    {  14, NULL, "i15v" },  // MAGNUM ROUNDS
    {  15, NULL, "i11v" },  // FUEL
    {  16, NULL, "i14v" },  // EXPLOSIVE R.
    {  17, NULL, "i12v" },  // ACID ROUNDS
    {  18, NULL, "i13v" },  // FLAME ROUNDS
    {  19, NULL, "i50v" },  // EMPTY BOTTLE
    {  20, NULL, "i21v" },  // WATER
    {  21, NULL, "i22v" },  // UMB No.2
    {  22, NULL, "i48v" },  // UMB No.4
    {  23, NULL, "i58v" },  // UMB No.7
    {  24, NULL, "i47v" },  // UMB No.13
    {  25, NULL, "i49v" },  // Yellow-6
    {  26, NULL, "i62v" },  // NP-003
    {  27, NULL, "i09v" },  // V-JOLT
    {  28, NULL, "i03v" },  // BROKEN SHOTGUN
    {  29, NULL, "i19v" },  // SQUARE CRANK
    {  30, NULL, "i72v" },  // HEX. CRANK
    {  31, NULL, "i38v" },  // EMBLEM
    {  32, NULL, "i39v" },  // GOLD EMBLEM
    {  33, NULL, "i41v" },  // BLUE JEWEL
    {  34, NULL, "i42v" },  // RED JEWEL
    {  35, NULL, "i45v" },  // MUSIC NOTES
    {  36, NULL, "i24v" },  // WOLF MEDAL
    {  37, NULL, "i25v" },  // EAGLE MEDAL
    {  38, NULL, "i43v" },  // HERBICIDE
    {  39, NULL, "i54v" },  // BATTERY
    {  40, NULL, "i37v" },  // MO DISK
    {  41, NULL, "i44v" },  // WIND CREST
    {  42, NULL, "i55v" },  // FLARE
    {  43, NULL, "i46v" },  // SLIDES
    {  44, NULL, "i60v" },  // MOON CREST
    {  45, NULL, "i61v" },  // STAR CREST
    {  46, NULL, "i59v" },  // SUN CREST
    {  47, NULL, "i56v" },  // INK RIBBON
    {  48, NULL, "i17v" },  // LIGHTER
    {  49, NULL, "i18v" },  // LOCKPICK
    {  50, "OIL", "i40v" },
    {  51, NULL, "i30v" },  // SWORD KEY
    {  52, NULL, "i28v" },  // ARMOR KEY
    {  53, NULL, "i29v" },  // SHIELD KEY
    {  54, NULL, "i27v" },  // HELMET KEY
    {  55, NULL, "i34v" },  // MASTER KEY
    {  56, NULL, "i73v" },  // CLOSET KEY
    {  57, NULL, "i32v" },  // 002 KEY
    {  58, NULL, "i31v" },  // 003 KEY
    {  59, NULL, "i36v" },  // C. ROOM KEY
    {  60, NULL, "i33v" },  // P. ROOM KEY
    {  61, NULL, "i63v" },  // DESK KEY
    {  62, NULL, "i53v" },  // BLANK BOOK
    {  63, NULL, "i51v" },  // DOOM BOOK 2
    {  64, NULL, "i52v" },  // DOOM BOOK 1
    {  65, NULL, "i23v" },  // F.-AID SPRAY
    {  66, NULL, "i20v" },  // SERUM
    {  67, NULL, "i66v" },  // RED HERB
    {  68, NULL, "i64v" },  // GREEN HERB
    {  69, NULL, "i65v" },  // BLUE HERB
    {  70, "%s (BLUE+RED)", "i69v" },
    {  71, "%s (2 GREEN)", "i67v" },
    {  72, "%s (GREEN+BLUE)", "i68v" },
    {  73, "%s (G+R+B)", "i70v" },
    {  74, "%s (3 GREEN)", "i71v" },
    {  75, "%s (2 GREEN+RED)", "i75v" },
    {  76, "PICK AXE", "i10v" },
    {  77, NULL, "i57v" },  // COM. RADIO
    {  78, "MAP MANSION 1F", "" },
    {  79, "MAP MANSION 2F", "i48v" },
    {  80, "MAP COURTYARD", "i52v" },
    {  81, "MAP UNDERGROUND", "" },
    {  82, "MAP GUARDHOUSE", "" },
    {  83, "MAP LABORATORY", "i50v" },
    { 111, "0x6f %s", "ING" },
    { 112, "0x70 %s", "MINI" },
    { 113, NULL, "IFLR" },  // FLARE PISTOL
    { 114, NULL, "IACD" },  // ACID PISTOL
    { 115, NULL, "IFRZ" },  // FREEZE PISTOL
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

// The names, resolved once on first use. Nothing they are built from changes
// while the game runs - the string tables are constant data - so once is enough.
#define ED_ITEM_NAME_CAP 40

static char s_itemNames[sizeof(g_edItems) / sizeof(g_edItems[0])][ED_ITEM_NAME_CAP];
static int  s_itemNamesBuilt = 0;

static const unsigned char* ed_game_item_name(int type)
{
    if (type == ITEM_GRENADE_PISTOL) return g_GrenadePistolNamePtr;
    if (type == ITEM_ACID_PISTOL)    return g_AcidPistolNamePtr;
    if (type == ITEM_FREEZE_PISTOL)  return g_FreezePistolNamePtr;
    if (type < 1 || type > 128) return NULL;
    return g_ItemNamePointers[type - 1];
}

static void ed_build_item_names(void)
{
    if (s_itemNamesBuilt) return;
    for (int i = 0; i < g_edItemCount; i++) {
        char game[ED_ITEM_NAME_CAP];
        ItemName_ToAscii(ed_game_item_name(g_edItems[i].type), game, (int)sizeof(game));
        const char* label = g_edItems[i].label;
        snprintf(s_itemNames[i], sizeof(s_itemNames[i]),
                 (label != NULL) ? label : "%s", game);
    }
    s_itemNamesBuilt = 1;
}

const char* EdContent_ItemNameAt(int index)
{
    if (index < 0 || index >= g_edItemCount) return "(unknown item)";
    ed_build_item_names();
    return s_itemNames[index];
}

const char* EdContent_ItemName(int type)
{
    for (int i = 0; i < g_edItemCount; i++)
        if (g_edItems[i].type == type) return EdContent_ItemNameAt(i);
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
