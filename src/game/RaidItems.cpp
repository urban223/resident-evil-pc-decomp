// RaidItems.cpp - the pickups lying in the RAID arena.
//
// CUSTOM. Not in the original game.
//
// WHY THIS IS NOT THE GAME'S OWN PICKUP PATH
//
// A story room's items are armed by the room's SCD script: the script sets up
// an AOT, the AOT posts global message 0xc0 when the player walks into it, and
// handle_message_post_action eventually reaches room_event_item_pickup, which
// reads the item id and quantity back out of the armed event record via
// g_pRoomActionEntry. Every step of that needs a compiled script in the RDT.
//
// The RAID room's RDT is a deliberately empty shell - it has no script at all,
// which is the whole reason a level can be a text file and reload on a keypress
// (RaidLevel.h). So the pickups are run from here instead: an array of points,
// a distance test, and one function that puts an item in a slot. That also
// keeps the arena's rules its own, which is what "отдельный режим, отдельная
// логика" asked for.
//
// The insert below is deliberately the CAREFUL version of what
// room_event_item_pickup does. The original tests g_selectedItemId rather than
// the item it is actually awarding, has no capacity check, and writes past the
// last slot when the inventory is full. Faithfully reproducing that would mean
// reproducing a memory stomp, so this one tests the item it is holding and
// refuses when there is nowhere to put it - the pickup simply stays on the
// floor, which is a state the player can see and act on.
#include "../Globals.h"
#include "Types.h"
#include "Entities.h"
#include "RaidLevel.h"
#include <cstring>

// How close counts as "in reach". The player's own radius is around 500 units
// and a pickup should be takeable while she is standing next to it, not on top
// of it, so this is a little over one body width.
#define RAID_ITEM_REACH   900

// The take button. 0x80 is ACTION - the same button that opens doors and
// examines things in a story room, which here has nothing else to do.
#define RAID_ITEM_BUTTON  0x80

// The confirmation cue. Bank 3 is the character sound bank, which is loaded for
// whoever is in the room, and is what the inventory menu uses for its own
// feedback (MainMenu.cpp). Change the id here if a different blip suits.
#define RAID_ITEM_SND_BANK  3
#define RAID_ITEM_SND_ID    5

static unsigned char s_taken[RAID_MAX_ITEM];
static unsigned char s_reach[RAID_MAX_ITEM];

void RaidItems_Reset(void)
{
    memset(s_taken, 0, sizeof(s_taken));
    memset(s_reach, 0, sizeof(s_reach));
}

int RaidItems_Taken(int i)
{
    return ((unsigned int)i < (unsigned int)RAID_MAX_ITEM) ? s_taken[i] : 1;
}

int RaidItems_Reach(int i)
{
    return ((unsigned int)i < (unsigned int)RAID_MAX_ITEM) ? s_reach[i] : 0;
}

// How many slots this character has: 8 for Jill, 6 for the others. The same
// expression CountHeldItems uses, so the two never disagree about what "full"
// means.
static int raid_slot_cap(void)
{
    return (4 - ((g_playerEntity.id & 3) != 1)) * 2;
}

// Which items merge into a slot they are already in. The ammunition block and
// ink ribbons - which is what the original means to test, before it reads the
// wrong variable.
static int raid_item_stacks(unsigned char id)
{
    return (id >= ITEM_CLIP && id <= ITEM_FLAME_ROUNDS) || id == ITEM_INK_RIBBONS;
}

// Put `qty` of `id` into the inventory. Returns 1 if all of it went in.
//
// Partial pickups are deliberately NOT supported: either the whole pile is
// taken or none of it is. Leaving four rounds of a clip on the floor is a state
// nothing in this mode can show the player.
static int raid_item_take(unsigned char id, unsigned char qty)
{
    unsigned char* slots = (unsigned char*)g_ItemSlotsPointer;
    if (slots == NULL || id == ITEM_NONE) return 0;

    const int cap  = raid_slot_cap();
    const int held = (int)g_TotalHeldItems;

    // How deep one slot goes. NOT g_ItemMaxQty - that table is the MAGAZINE
    // size (the reload reads it as g_ItemMaxQty[(weaponId + 9) * 4]), which is
    // 15 for a clip. A pile of 60 rounds is one slot holding 60, exactly as
    // this mode's own starting loadout already has it. The real ceiling is the
    // one room_event_item_pickup stops at.
    const unsigned int max = 0xfa;

    if (raid_item_stacks(id)) {
        // Room in the slots already holding this, then a new slot for whatever
        // is left. Counted first, because a pickup that cannot be taken whole
        // must not half-empty itself into the inventory.
        unsigned int room = 0;
        for (int i = 0; i < held; i++) {
            if (slots[i*2] != id) continue;
            if (slots[i*2+1] < max) room += max - slots[i*2+1];
        }
        if (held < cap) room += max;
        if (room < (unsigned int)qty) return 0;

        for (int i = 0; i < held && qty; i++) {
            if (slots[i*2] != id) continue;
            unsigned int have = slots[i*2+1];
            if (have >= max) continue;
            unsigned int put = max - have;
            if (put > (unsigned int)qty) put = qty;
            slots[i*2+1] = (unsigned char)(have + put);
            qty = (unsigned char)(qty - put);
        }
        if (qty == 0) { LoadHeldItemsImages(); return 1; }
    }

    if (held >= cap) return 0;

    // The first empty slot is always the one at `held`: CountHeldItems stops at
    // the first zero, so the inventory is never sparse.
    if ((unsigned int)qty > max) qty = (unsigned char)max;
    slots[held*2]     = id;
    slots[held*2 + 1] = qty;
    LoadHeldItemsImages();
    return 1;
}

// The starting inventory, from the level's `give` lines. Returns 0 when the
// level has none, and then the caller keeps the mode's built-in loadout - a
// level that simply does not mention the inventory should not empty it.
int RaidItems_Give(void)
{
    if (!g_raidLevel.loaded || g_raidLevel.ngive <= 0) return 0;

    unsigned char* slots = (unsigned char*)g_ItemSlotsPointer;
    if (slots == NULL) return 0;

    const int cap = raid_slot_cap();
    int used = 0;
    for (int i = 0; i < g_raidLevel.ngive && used < cap; i++) {
        const RaidGive* G = &g_raidLevel.give[i];
        if (G->type == ITEM_NONE) continue;
        slots[used*2]     = G->type;
        slots[used*2 + 1] = G->amount;
        used++;
    }
    for (int i = used; i < cap; i++) {
        slots[i*2] = 0;
        slots[i*2 + 1] = 0;
    }

    // LoadHeldItemsImages recounts the slots and rebuilds the bitmask, the
    // slot indices and the HUD icons from whatever is in them, so writing the
    // slots and calling it is the whole of it.
    LoadHeldItemsImages();
    return used > 0;
}

// ---------------------------------------------------------------------------
// Once a frame, from the render flush (Rendering.cpp), beside the other RAID
// updates. Not from the player's own behaviour tree: this has to run whatever
// she is doing, and an arena pickup is not one of the behaviours.
// ---------------------------------------------------------------------------
void RaidItems_Update(void)
{
    if (g_raidMode == 0 || !g_raidLevel.loaded) return;

    const int px = g_playerEntity.position.x;
    const int pz = g_playerEntity.position.z;
    const int take = (g_PlayerDpadPressed & RAID_ITEM_BUTTON) != 0;

    // The nearest one in reach wins, so two pickups lying together are taken
    // one press at a time and the player always gets the one she is standing
    // over rather than whichever happens to come first in the file.
    int best = -1;
    int bestD = 0;

    for (int i = 0; i < g_raidLevel.nitem && i < RAID_MAX_ITEM; i++) {
        s_reach[i] = 0;
        if (s_taken[i]) continue;
        const RaidItem* I = &g_raidLevel.item[i];
        const int dx = px - (int)I->x;
        const int dz = pz - (int)I->z;
        const int d2 = dx*dx + dz*dz;
        if (d2 > RAID_ITEM_REACH * RAID_ITEM_REACH) continue;
        s_reach[i] = 1;
        if (best < 0 || d2 < bestD) { best = i; bestD = d2; }
    }

    if (take && best >= 0) {
        const RaidItem* I = &g_raidLevel.item[best];
        if (raid_item_take(I->type, I->amount)) {
            s_taken[best] = 1;
            s_reach[best] = 0;
            play_sfx(RAID_ITEM_SND_BANK, RAID_ITEM_SND_ID, 0);
        }
    }
}
