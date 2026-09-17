// PanelPlace.cpp - Place Actors, and the content browser under it.
//
// CUSTOM (port-only).
//
// Two panels, one file, because they are two halves of the same question. The
// top one places the things a level is built from and there are six of them;
// the bottom one chooses WHICH pickup or WHICH enemy, out of a hundred, and
// that needs a search box and a scrolling list.
//
// The split matters: you place a Beretta, not a generic pickup. So the content
// browser holds the current choice and the palette's Item button places it,
// and double-clicking an entry in the browser places it directly.
#include "../EditorShell.h"
#include "../EditorState.h"
#include "../EditorContent.h"
#include "../../RaidLevel.h"
#include "../../RaidItemModels.h"
#include "../../../Globals.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Place Actors
// ---------------------------------------------------------------------------
struct PlaceEntry {
    const char* name;
    const char* hint;
    int         icon;
};

void EdPanel_Place(EdRect r)
{
    EditorShell* S = &g_edShell;
    EdUI_Fill(r, EDC_PANEL);
    EdRect body = EdPanel_Header(r, EDUI_ICON_PLUS, "Place Actors", NULL);
    EdUI_Fill(body, EDC_PANEL_ALT);

    static const PlaceEntry kEntries[] = {
        { "Wall / Box",  "solid geometry",          EDUI_ICON_BOX },
        { "Item Pickup", "from the browser below",  EDUI_ICON_ITEM },
        { "Enemy",       "spawns when you Play",    EDUI_ICON_ENEMY },
        { "Light",       "three per level",         EDUI_ICON_LIGHT },
        { "Camera",      "from the current view",   EDUI_ICON_CAMERA },
        { "Camera Zone", "switches camera",         EDUI_ICON_ZONE },
    };
    const int n = (int)(sizeof(kEntries) / sizeof(kEntries[0]));
    // Two lines of text in a row: the name's line box is 27 units tall and
    // the hint's 22, so a 38-unit row would have them overlapping. They are
    // placed as two boxes and centred inside those instead of stacked by line
    // height, which is what keeps the pair tight without them touching.
    const float rowH = 42.0f;

    EdRect page = EdUI_ScrollBegin(EdUI_Id("place"), body, n * rowH + EDM_GAP);
    float y = page.y + EDM_GAP * 0.5f;

    for (int i = 0; i < n; i++) {
        const EdRect row = EdR(page.x + EDM_GAP * 0.5f, y,
                               page.w - EDM_GAP, rowH - 4.0f);
        y += rowH;

        const EdId id = EdUI_IdIdx("place", i);
        int held = 0;
        const int clicked = EdUI_Clickable(id, row, &held);
        const int over = EdUI_IsHot(id) || held;

        EdUI_Plate(row, EDUI_TILE_ROUND3, over ? 0xFF343A41u : 0xFF262A2Fu);
        if (held) EdUI_Plate(row, EDUI_TILE_ROUND3, EDC_PRESS);

        EdUI_Icon(EdR(row.x + 8.0f, row.y + (row.h - 22.0f) * 0.5f, 22.0f, 22.0f),
                  kEntries[i].icon, over ? EDC_ACCENT : EDC_TEXT_DIM);
        const float tx = row.x + 38.0f;
        const float tw = row.w - 44.0f;
        EdUI_TextIn(EDUI_FONT_UI, kEntries[i].name,
                    EdR(tx, row.y + 3.0f, tw, 18.0f), ED_ALIGN_LEFT, EDC_TEXT);
        EdUI_TextIn(EDUI_FONT_SMALL, kEntries[i].hint,
                    EdR(tx, row.y + 20.0f, tw, 15.0f), ED_ALIGN_LEFT,
                    EDC_TEXT_FAINT);

        if (clicked) {
            switch (i) {
            case 0: EdAct_AddBox(); break;
            case 1: EdAct_AddItem(S->contentTab == 0 ? S->contentPick : 11); break;
            case 2: EdAct_AddEnemy(S->contentTab == 1 ? S->contentPick : 0); break;
            case 3: EdAct_AddLight(); break;
            case 4: EdAct_AddCamera(); break;
            default: EdAct_AddZone(); break;
            }
        }
    }
    EdUI_ScrollEnd();
}

// ---------------------------------------------------------------------------
// Content browser
// ---------------------------------------------------------------------------
static int matches(const char* name, const char* needle)
{
    if (needle == NULL || *needle == '\0') return 1;
    // Case-insensitive substring; the names are upper case and nobody types
    // that way.
    for (const char* a = name; *a; a++) {
        const char* p = a;
        const char* q = needle;
        while (*q) {
            char ca = *p, cq = *q;
            if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
            if (cq >= 'a' && cq <= 'z') cq = (char)(cq - 32);
            if (ca != cq) break;
            p++; q++;
        }
        if (*q == '\0') return 1;
    }
    return 0;
}

// Tabs and the search field, cut off the top of the panel's body.
static void content_tabs(EdRect* body)
{
    EditorShell* S = &g_edShell;
    EdRect bar = EdR_Cut(body, 28.0f, ED_SIDE_TOP);
    EdUI_Fill(bar, EDC_PANEL);
    EdRect brow = EdR_Inset(bar, 4.0f);

    const float tabW = 58.0f;
    if (EdUI_Button(EdUI_Id("content.items"),
                    EdR_Cut(&brow, tabW, ED_SIDE_LEFT), "Items", -1,
                    ED_BTN_FLAT | ED_BTN_IF_ON(S->contentTab == 0))) {
        S->contentTab = 0;
        S->contentPick = 11;
    }
    if (EdUI_Button(EdUI_Id("content.enemies"),
                    EdR_Cut(&brow, tabW + 12.0f, ED_SIDE_LEFT), "Enemies", -1,
                    ED_BTN_FLAT | ED_BTN_IF_ON(S->contentTab == 1))) {
        S->contentTab = 1;
        S->contentPick = 0;
    }
    EdR_Cut(&brow, 4.0f, ED_SIDE_LEFT);
    EdUI_Icon(EdR_Cut(&brow, 16.0f, ED_SIDE_LEFT), EDUI_ICON_SEARCH,
              EDC_TEXT_FAINT);
    EdUI_TextField(EdUI_Id("content.search"), brow, S->search,
                   (int)sizeof(S->search));
}

// The items or enemies matching the search, as a scrolling list. Clicking one
// makes it the pick the Place button and the palette use.
static void content_list(EdRect body)
{
    EditorShell* S = &g_edShell;
    const int count = (S->contentTab == 0) ? g_edItemCount : g_edEnemyCount;
    int shown = 0;
    for (int i = 0; i < count; i++) {
        const char* nm = (S->contentTab == 0) ? EdContent_ItemNameAt(i)
                                              : g_edEnemyNames[i];
        if (matches(nm, S->search)) shown++;
    }

    EdRect page = EdUI_ScrollBegin(EdUI_Id("content.list"), body,
                                   shown * EDM_ROW_H + 4.0f);
    float y = page.y + 2.0f;

    for (int i = 0; i < count; i++) {
        const char* nm = (S->contentTab == 0) ? EdContent_ItemNameAt(i)
                                              : g_edEnemyNames[i];
        if (!matches(nm, S->search)) continue;

        const int type = (S->contentTab == 0) ? g_edItems[i].type : i;
        const EdRect row = EdR(page.x, y, page.w, EDM_ROW_H);
        y += EDM_ROW_H;
        if (row.y + row.h < body.y || row.y > body.y + body.h) continue;

        const int selected = (S->contentPick == type);
        char suffix[16];
        suffix[0] = '\0';
        if (S->contentTab == 0) {
            // Whether the pickup will have a real model is worth knowing
            // before it is placed, not after.
            snprintf(suffix, sizeof(suffix), "%s",
                     (g_edItems[i].model[0] != '\0') ? g_edItems[i].model : "-");
        }

        const int icon = (S->contentTab == 0) ? EDUI_ICON_ITEM : EDUI_ICON_ENEMY;
        if (EdUI_Row(EdUI_IdIdx("content", i), row, 0, icon, nm, suffix,
                     selected, NULL)) {
            S->contentPick = type;
        }
    }
    EdUI_ScrollEnd();
}

// The action: place whatever the list has picked.
static void content_footer(EdRect foot)
{
    EditorShell* S = &g_edShell;
    EdUI_Fill(foot, EDC_PANEL);
    EdUI_Fill(EdR(foot.x, foot.y, foot.w, 1.0f), EDC_BORDER);
    EdRect place = EdR_Inset(foot, 5.0f);
    char label[64];
    if (S->contentTab == 0) {
        snprintf(label, sizeof(label), "Place %s",
                 EdContent_ItemName(S->contentPick));
    } else {
        snprintf(label, sizeof(label), "Place %s",
                 EdContent_EnemyName(S->contentPick));
    }
    if (EdUI_Button(EdUI_Id("content.place"), place, label, EDUI_ICON_PLUS,
                    ED_BTN_ACCENT)) {
        if (S->contentTab == 0) EdAct_AddItem(S->contentPick);
        else                    EdAct_AddEnemy(S->contentPick);
    }
}

void EdPanel_Content(EdRect r)
{
    EditorShell* S = &g_edShell;
    EdUI_Fill(r, EDC_PANEL);

    char note[24];
    snprintf(note, sizeof(note), "%s",
             S->contentTab == 0 ? "items" : "enemies");
    EdRect body = EdPanel_Header(r, EDUI_ICON_FOLDER, "Content", note);

    content_tabs(&body);

    // The footer is carved off before the list is laid out: a list that runs
    // under the button it is being placed with hides its own last row.
    EdRect foot = EdR_Cut(&body, 34.0f, ED_SIDE_BOTTOM);
    EdUI_Fill(body, EDC_PANEL_ALT);

    content_list(body);
    content_footer(foot);
}
