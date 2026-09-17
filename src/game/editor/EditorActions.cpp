// EditorActions.cpp - the commands, in one place.
//
// CUSTOM (port-only).
//
// A command is reachable three ways - a menu item, a toolbar button, a key -
// and all three call in here, so there is one description of what "delete"
// means and not three that drift. This file is also the only one that grows
// the level's arrays, which keeps the bounds checks together.
#include "EditorShell.h"
#include "EditorState.h"
#include "EditorContent.h"
#include "../RaidLevel.h"
#include "../RaidItemModels.h"
#include "../RaidEnemies.h"
#include "../Entities.h"
#include "../../Globals.h"

#include <string.h>

int EdAct_Snap(int v)
{
    const int s = g_edShell.snapMove;
    if (s <= 0) return v;
    const int half = s / 2;
    return (v >= 0) ? ((v + half) / s) * s : -(((-v + half) / s) * s);
}

void EdAct_PlacePoint(int* x, int* z)
{
    if (x) *x = EdAct_Snap((int)g_edCam.tx);
    if (z) *z = EdAct_Snap((int)g_edCam.tz);
}

static void select_last(int kind, int index)
{
    g_edSel.kind = kind;
    g_edSel.index = index;
}

// ---------------------------------------------------------------------------
// Placing
// ---------------------------------------------------------------------------
void EdAct_AddBox(void)
{
    RaidLevel* L = &g_raidLevel;
    if (!L->loaded || L->nbox >= RAID_MAX_BOX) return;

    int cx, cz;
    EdAct_PlacePoint(&cx, &cz);
    const int half = 600;
    const int height = 1800;

    RaidBox* B = &L->box[L->nbox];
    memset(B, 0, sizeof(*B));
    B->x0 = (short)(cx - half);
    B->x1 = (short)(cx + half);
    B->z0 = (short)(cz - half);
    B->z1 = (short)(cz + half);
    // Y is NEGATIVE upwards in this engine: the top of a box on the floor is
    // the more negative number, and getting this backwards buries the wall.
    B->y0 = (short)(-height);
    B->y1 = 0;
    B->flags = (unsigned short)(RAID_BOX_DRAW | RAID_BOX_SOLID);
    B->shade = 0.75f;
    B->tr = 190; B->tg = 190; B->tb = 195;

    select_last(ED_BOX, L->nbox);
    L->nbox++;
    RaidLevel_Apply();          // the collision table is rebuilt from the boxes
}

void EdAct_AddItem(int type)
{
    RaidLevel* L = &g_raidLevel;
    if (!L->loaded || L->nitem >= RAID_MAX_ITEM) return;

    int cx, cz;
    EdAct_PlacePoint(&cx, &cz);

    RaidItem* I = &L->item[L->nitem];
    I->x = (short)cx;
    I->z = (short)cz;
    I->angle = 0;
    I->type = (unsigned char)type;
    I->amount = (unsigned char)(EdContent_ItemStacks(type) ? 15 : 1);

    select_last(ED_ITEM, L->nitem);
    L->nitem++;
    RaidItems_Reset();
    RaidItemModels_Sync();      // a type not seen before needs its .ivm
}

void EdAct_AddEnemy(int type)
{
    RaidLevel* L = &g_raidLevel;
    if (!L->loaded || L->nenemy >= RAID_MAX_ENEMY) return;

    int cx, cz;
    EdAct_PlacePoint(&cx, &cz);

    RaidEnemy* E = &L->enemy[L->nenemy];
    E->x = (short)cx;
    E->z = (short)cz;
    E->angle = 0;
    E->type = (unsigned char)type;

    select_last(ED_ENEMY, L->nenemy);
    L->nenemy++;
}

void EdAct_AddLight(void)
{
    RaidLevel* L = &g_raidLevel;
    if (!L->loaded || L->nlight >= RAID_MAX_LIGHT) return;

    int cx, cz;
    EdAct_PlacePoint(&cx, &cz);

    RaidLight* G = &L->light[L->nlight];
    G->x = cx;
    G->y = -2500;               // head height, upwards being negative
    G->z = cz;
    G->r = 128; G->g = 120; G->b = 110;
    G->radius = 6000;

    select_last(ED_LIGHT, L->nlight);
    L->nlight++;
    RaidLevel_Apply();
}

void EdAct_AddCamera(void)
{
    RaidLevel* L = &g_raidLevel;
    if (!L->loaded || L->ncam >= RAID_MAX_CAM) return;

    float ex, ey, ez;
    EditorCamera_Eye(&ex, &ey, &ez);
    RaidCam* C = &L->cam[L->ncam];
    C->fx = (int)ex; C->fy = (int)ey; C->fz = (int)ez;
    C->tx = (int)g_edCam.tx; C->ty = (int)g_edCam.ty; C->tz = (int)g_edCam.tz;
    C->fov = g_edCam.fov;

    select_last(ED_CAM, L->ncam);
    L->ncam++;
}

void EdAct_AddZone(void)
{
    RaidLevel* L = &g_raidLevel;
    if (!L->loaded || L->nzone >= RAID_MAX_ZONE) return;

    int cx, cz;
    EdAct_PlacePoint(&cx, &cz);
    const int half = 1500;

    RaidZone* Z = &L->zone[L->nzone];
    Z->cam = 0;
    Z->x0 = (short)(cx - half);
    Z->x1 = (short)(cx + half);
    Z->z0 = (short)(cz - half);
    Z->z1 = (short)(cz + half);

    select_last(ED_ZONE, L->nzone);
    L->nzone++;
    RaidLevel_Apply();
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
void EdAct_Duplicate(void)
{
    RaidLevel* L = &g_raidLevel;
    if (!L->loaded || !EditorSelect_Valid()) return;

    // The copy is offset by one snap step so it is not exactly on top of the
    // original - an invisible duplicate is worse than no duplicate.
    const int step = (g_edShell.snapMove > 0) ? g_edShell.snapMove : 400;

    switch (g_edSel.kind) {
    case ED_BOX:
        if (L->nbox >= RAID_MAX_BOX) return;
        L->box[L->nbox] = L->box[g_edSel.index];
        L->box[L->nbox].x0 = (short)(L->box[L->nbox].x0 + step);
        L->box[L->nbox].x1 = (short)(L->box[L->nbox].x1 + step);
        select_last(ED_BOX, L->nbox);
        L->nbox++;
        RaidLevel_Apply();
        break;
    case ED_ITEM:
        if (L->nitem >= RAID_MAX_ITEM) return;
        L->item[L->nitem] = L->item[g_edSel.index];
        L->item[L->nitem].x = (short)(L->item[L->nitem].x + step);
        select_last(ED_ITEM, L->nitem);
        L->nitem++;
        RaidItems_Reset();
        break;
    case ED_ENEMY:
        if (L->nenemy >= RAID_MAX_ENEMY) return;
        L->enemy[L->nenemy] = L->enemy[g_edSel.index];
        L->enemy[L->nenemy].x = (short)(L->enemy[L->nenemy].x + step);
        select_last(ED_ENEMY, L->nenemy);
        L->nenemy++;
        break;
    case ED_LIGHT:
        if (L->nlight >= RAID_MAX_LIGHT) return;
        L->light[L->nlight] = L->light[g_edSel.index];
        L->light[L->nlight].x += step;
        select_last(ED_LIGHT, L->nlight);
        L->nlight++;
        RaidLevel_Apply();
        break;
    case ED_ZONE:
        if (L->nzone >= RAID_MAX_ZONE) return;
        L->zone[L->nzone] = L->zone[g_edSel.index];
        L->zone[L->nzone].x0 = (short)(L->zone[L->nzone].x0 + step);
        L->zone[L->nzone].x1 = (short)(L->zone[L->nzone].x1 + step);
        select_last(ED_ZONE, L->nzone);
        L->nzone++;
        break;
    default:
        break;
    }
}

void EdAct_Delete(void)      { EditorSelect_Delete(); }
void EdAct_Focus(void)       { EditorCamera_FrameSelection(); }
void EdAct_FrameAll(void)    { EditorCamera_FrameLevel(); }
void EdAct_Save(void)        { EditorSave_Write(); }

void EdAct_Reload(void)
{
    // The same request the reload key raises, consumed where it is safe to
    // swap the arrays out - not in the middle of a panel walking them.
    g_raidReloadRequest = 1;
    EditorSelect_Clear();
}

void EdAct_CameraFromView(void)
{
    RaidLevel* L = &g_raidLevel;
    if (!L->loaded || L->ncam <= 0) return;
    int i = (g_edSel.kind == ED_CAM && EditorSelect_Valid()) ? g_edSel.index : 0;
    if (i >= L->ncam) i = 0;

    float ex, ey, ez;
    EditorCamera_Eye(&ex, &ey, &ez);
    RaidCam* C = &L->cam[i];
    C->fx = (int)ex; C->fy = (int)ey; C->fz = (int)ez;
    C->tx = (int)g_edCam.tx; C->ty = (int)g_edCam.ty; C->tz = (int)g_edCam.tz;
    C->fov = g_edCam.fov;
    RaidLevel_Apply();
}
