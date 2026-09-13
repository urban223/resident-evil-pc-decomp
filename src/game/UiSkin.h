// UiSkin.h - CUSTOM (port-only): the Space GUI skin for the status screen.
//
// The engine keeps drawing what it owns - item icons, quantities, the
// portrait, the examined item's model - and this draws everything around
// them: plates, cells, the info card, the condition block and the technical
// filler. It never reads the menu's own statics; MainMenu.cpp fills the state
// struct below and calls in, which keeps the two sides honest about what the
// skin is allowed to know.
//
// Layout is specified in claude/status-screen-skin-spec.md and is in the
// game's 320x240 logical space, the same space menu_draw_inventory uses.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct UiSkinState {
    int slotCount;      // 8 (Jill) or 6 (Chris)
    int heldCount;      // filled cells; RE1 packs items from slot 0 up
    int selected;       // slot under the cursor, -1 for none
    int equipped;       // slot holding the equipped weapon, -1 for none
    int other;          // slot shown in the OTHER box, -1 for none
    int points;
    int frame;          // presented-frame counter: drives every animation
    int tab;            // highlighted top tab, 0..3 (MAP/FILE/RADIO/EXIT), -1 none
    int radioOn;        // 0 greys the RADIO tab, as the original does

    const char* characterName;
    const char* affiliation;
    const char* condition;        // FINE / CAUTION / DANGER / POISON
    unsigned int conditionColor;  // 0xAARRGGBB, matches the game's own colours
    int conditionLevel;           // the game's own 0=danger 1=caution 2..3=fine
                                  // 4=poison, which sets the trace's rate and
                                  // height the way the original EKG does

    int achvUnlocked;   // for the pip row under POINTS
    int achvTotal;

    int ekgHead;        // where the game's EKG is writing, 0..1000 across the
                        // trace, so the block's sweep column tracks the real
                        // one instead of running on a period of its own

    const char* itemName;         // of the selected item; NULL hides the card
    const char* itemType;
    const char* itemDesc;         // the game's own examine text, decoded to
                                  // ASCII as ONE string: the table's own line
                                  // break is for a 26-column line and means
                                  // nothing at the card's width, so the skin
                                  // re-wraps it itself
    int descLarge;                // draw the description in the title font:
                                  // the item viewer is open and the card is
                                  // where that text is being read

    // The item's action menu (USE/EQUIP, CHECK, COMBINE). RE1 draws it as a
    // 48x24 sprite box at a fixed spot over the inventory; the skin opens it
    // beside the selected cell instead, the way RE2 Remake does.
    int actionOpen;     // 0 closed, 1..8 the game's own open/close counter
    int actionIndex;    // highlighted row, 0..2
    int actionEquip;    // row 0 reads EQUIP rather than USE

    // Game-space Y of the message line while one is up ("You don't need to use
    // this item at the moment."), 0 when none. The message glyphs go through
    // AddTintSprite at depth 450-482, below everything the skin queues, so
    // they land on top of it with nothing behind them; the skin puts a plate
    // under them rather than trying to take the text over.
    int msgY;
};

// 1 while the frozen room should stay visible behind the skin. The renderer
// asks at present time (OT_InsertPrimitive drops the room quad for every
// standalone screen), and MainMenu.cpp sets it for exactly the frames the
// status screen is the thing on top - not while a tab screen, which draws its
// own opaque background, is open over it.
int  UiSkin_RoomVisible(void);
void UiSkin_SetRoomVisible(int on);

// 1 when the skin should replace the original chrome. MainMenu.cpp asks before
// gating its own frame parts off, so a single switch swaps the whole screen.
int  UiSkin_Enabled(void);
void UiSkin_SetEnabled(int on);

// Where the skin wants the engine to draw the things it still owns. Filled in
// game-space pixels; menu_draw_inventory uses these instead of the original
// slot table while the skin is on.
void UiSkin_SlotRect(int slot, short* x, short* y);
void UiSkin_EquippedRect(short* x, short* y);
void UiSkin_OtherRect(short* x, short* y);
void UiSkin_PortraitRect(short* x, short* y);

// Where the game's own EKG belongs. menu_draw_health_bar still builds the
// trace from its wave tables; MainMenu.cpp maps each line primitive from the
// original screen's EKG box into this rect on the way out, so the heartbeat in
// the condition block is the real one rather than a decorative stand-in.
void UiSkin_EkgRect(short* x, short* y, short* w, short* h);

// Draw the skin. Call once per frame from the status screen, before the
// engine's own icon draws - the plates queue behind them by depth anyway, but
// keeping the order means the queue fills in reading order.
void UiSkin_DrawStatus(const struct UiSkinState* st);

#ifdef __cplusplus
}
#endif
