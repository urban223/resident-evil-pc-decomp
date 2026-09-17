// PanelOutliner.cpp - everything in the level, as a list.
//
// CUSTOM (port-only).
//
// The level is six flat arrays, so the tree is two levels deep: a category
// per array, and a row per entry. That is not a simplification of a richer
// structure, it IS the structure, and pretending otherwise with folders that
// hold nothing would only add clicks.
//
// A row's identity is its category and index, matching EditorSel exactly, so
// clicking here and clicking in the viewport select the same way and neither
// has to translate.
#include "../EditorShell.h"
#include "../EditorState.h"
#include "../EditorContent.h"
#include "../../RaidLevel.h"
#include "../../../Globals.h"

#include <stdio.h>
#include <string.h>

struct EdGroup {
    int         kind;
    const char* name;
    int         icon;
    int         count;
};

static void group_table(EdGroup* g, int* n)
{
    const RaidLevel* L = &g_raidLevel;
    int i = 0;
    g[i].kind = ED_BOX;   g[i].name = "Geometry"; g[i].icon = EDUI_ICON_BOX;    g[i].count = L->nbox;   i++;
    g[i].kind = ED_ITEM;  g[i].name = "Pickups";  g[i].icon = EDUI_ICON_ITEM;   g[i].count = L->nitem;  i++;
    g[i].kind = ED_ENEMY; g[i].name = "Enemies";  g[i].icon = EDUI_ICON_ENEMY;  g[i].count = L->nenemy; i++;
    g[i].kind = ED_LIGHT; g[i].name = "Lights";   g[i].icon = EDUI_ICON_LIGHT;  g[i].count = L->nlight; i++;
    g[i].kind = ED_CAM;   g[i].name = "Cameras";  g[i].icon = EDUI_ICON_CAMERA; g[i].count = L->ncam;   i++;
    g[i].kind = ED_ZONE;  g[i].name = "Zones";    g[i].icon = EDUI_ICON_ZONE;   g[i].count = L->nzone;  i++;
    g[i].kind = ED_SPAWN; g[i].name = "Player Start"; g[i].icon = EDUI_ICON_SPAWN; g[i].count = 1;      i++;
    *n = i;
}

// What one entry is called. The name carries the thing that distinguishes it
// from its neighbours - an item's item, an enemy's species, a box's size -
// because "Box 7" is not something anybody can look for.
static void row_label(int kind, int index, char* out, int cap,
                      char* suffix, int scap)
{
    const RaidLevel* L = &g_raidLevel;
    suffix[0] = '\0';
    switch (kind) {
    case ED_BOX: {
        const RaidBox* B = &L->box[index];
        const int w = B->x1 - B->x0, d = B->z1 - B->z0;
        const int h = (B->y1 > B->y0) ? (B->y1 - B->y0) : (B->y0 - B->y1);
        snprintf(out, (size_t)cap, "Box %02d", index);
        snprintf(suffix, (size_t)scap, "%dx%dx%d", w, h, d);
        break;
    }
    case ED_ITEM: {
        const RaidItem* I = &L->item[index];
        snprintf(out, (size_t)cap, "%s", EdContent_ItemName(I->type));
        if (EdContent_ItemStacks(I->type))
            snprintf(suffix, (size_t)scap, "x%d", I->amount);
        break;
    }
    case ED_ENEMY:
        snprintf(out, (size_t)cap, "%s", EdContent_EnemyName(L->enemy[index].type));
        break;
    case ED_LIGHT: {
        const RaidLight* G = &L->light[index];
        snprintf(out, (size_t)cap, "Light %d", index);
        snprintf(suffix, (size_t)scap, "r%d", G->radius);
        break;
    }
    case ED_CAM:
        snprintf(out, (size_t)cap, "Camera %d", index);
        snprintf(suffix, (size_t)scap, "fov %d", L->cam[index].fov);
        break;
    case ED_ZONE:
        snprintf(out, (size_t)cap, "Zone %d", index);
        snprintf(suffix, (size_t)scap, "-> cam %d", L->zone[index].cam);
        break;
    default:
        snprintf(out, (size_t)cap, "Player Start");
        break;
    }
}

void EdPanel_Outliner(EdRect r)
{
    EditorShell* S = &g_edShell;
    EdUI_Fill(r, EDC_PANEL);

    char note[32];
    const RaidLevel* L = &g_raidLevel;
    const int total = L->nbox + L->nitem + L->nenemy + L->nlight + L->ncam
                    + L->nzone + 1;
    snprintf(note, sizeof(note), "%d actors", total);
    EdRect body = EdPanel_Header(r, EDUI_ICON_MENU, "World Outliner", note);
    EdUI_Fill(body, EDC_PANEL_ALT);

    EdGroup g[8];
    int ngroup = 0;
    group_table(g, &ngroup);

    float contentH = 0.0f;
    for (int i = 0; i < ngroup; i++) {
        contentH += EDM_ROW_H;
        if (S->groupOpen[i]) contentH += g[i].count * EDM_ROW_H;
    }

    EdRect page = EdUI_ScrollBegin(EdUI_Id("outliner"), body, contentH);
    float y = page.y;

    for (int i = 0; i < ngroup; i++) {
        char head[48];
        snprintf(head, sizeof(head), "%s", g[i].name);
        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d", g[i].count);

        const EdRect hr = EdR(page.x, y, page.w, EDM_ROW_H);
        // The category row is drawn a shade above its children so the list
        // has structure even when every twisty is open.
        if (hr.y + hr.h > body.y && hr.y < body.y + body.h)
            EdUI_Fill(hr, 0x18FFFFFFu);
        EdUI_Row(EdUI_IdIdx("outgroup", i), hr, 0, g[i].icon, head, cnt, 0,
                 &S->groupOpen[i]);
        y += EDM_ROW_H;

        if (!S->groupOpen[i]) continue;

        for (int k = 0; k < g[i].count; k++) {
            const EdRect rr = EdR(page.x, y, page.w, EDM_ROW_H);
            y += EDM_ROW_H;
            // Rows outside the view are skipped entirely: a level with 128
            // boxes would otherwise cost 128 clipped-away draws a frame.
            if (rr.y + rr.h < body.y || rr.y > body.y + body.h) continue;

            char label[64], suffix[24];
            row_label(g[i].kind, k, label, sizeof(label), suffix, sizeof(suffix));
            const int selected = (g_edSel.kind == g[i].kind
                               && g_edSel.index == k && g[i].kind != ED_SPAWN)
                              || (g[i].kind == ED_SPAWN && g_edSel.kind == ED_SPAWN);

            if (EdUI_Row(EdUI_IdIdx(g[i].name, k), rr, 1, -1, label, suffix,
                         selected, NULL)) {
                g_edSel.kind = g[i].kind;
                g_edSel.index = k;
            }
        }
    }

    EdUI_ScrollEnd();
}
