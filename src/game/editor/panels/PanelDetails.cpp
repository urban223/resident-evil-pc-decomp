// PanelDetails.cpp - what the selected thing is, and how to change it.
//
// CUSTOM (port-only).
//
// One section per aspect - transform, shape, appearance - and inside each, a
// row per field. Every field writes straight into g_raidLevel and, where the
// engine caches something derived from it, calls the one function that
// rebuilds that cache: RaidLevel_Apply for collision, lights and cameras,
// RaidItems_Reset for the pickups, RaidItemModels_Sync when a type changes and
// a model has to be loaded. Nothing is deferred to a "commit" - the room in
// the viewport is the level, so an edit that has not landed yet would be an
// edit you cannot see.
#include "../EditorShell.h"
#include "../EditorState.h"
#include "../EditorContent.h"
#include "../../RaidLevel.h"
#include "../../RaidItemModels.h"
#include "../../../Globals.h"

#include <stdio.h>
#include <string.h>

#define LABEL_W 78.0f

// A field's identity is its LABEL, and for a multi-component field the label
// plus the component index. Not the address of what it edits: this is a 32-bit
// project (AGENTS.md, Code style) and hashing a pointer to make an id drags a
// width assumption into a panel that has no business holding one. The label is
// unique within a panel, and only one panel's fields are drawn at a time,
// which is all an immediate-mode id has to be.

// A field that edits a short. The int detour is so one DragInt serves every
// field; the clamp is the short's range, which is also the level format's.
static int field_short(EdRect* body, const char* label, short* v,
                       int lo, int hi, const char* suffix)
{
    const EdRect box = EdUI_FieldRow(body, label, LABEL_W);
    int t = *v;
    const EdId id = EdUI_Id(label);
    if (EdUI_DragInt(id, box, &t, 4.0f, lo, hi, suffix)) {
        *v = (short)t;
        return 1;
    }
    return 0;
}

static int field_int(EdRect* body, const char* label, int* v,
                     int lo, int hi, float speed, const char* suffix)
{
    const EdRect box = EdUI_FieldRow(body, label, LABEL_W);
    const EdId id = EdUI_Id(label);
    return EdUI_DragInt(id, box, v, speed, lo, hi, suffix);
}

static int field_byte(EdRect* body, const char* label, unsigned char* v,
                      int lo, int hi)
{
    const EdRect box = EdUI_FieldRow(body, label, LABEL_W);
    int t = *v;
    const EdId id = EdUI_Id(label);
    if (EdUI_DragInt(id, box, &t, 0.7f, lo, hi, "")) {
        *v = (unsigned char)t;
        return 1;
    }
    return 0;
}

// An angle, shown in degrees and stored in the engine's 4096ths of a turn.
static int field_angle(EdRect* body, const char* label, short* v)
{
    const EdRect box = EdUI_FieldRow(body, label, LABEL_W);
    int deg = (((int)*v & 0xFFF) * 360) / 4096;
    const EdId id = EdUI_Id(label);
    if (EdUI_DragInt(id, box, &deg, 0.6f, 0, 359, " deg")) {
        *v = (short)((deg * 4096) / 360);
        return 1;
    }
    return 0;
}

// A colour, as three bytes on one line with the swatch beside them.
static int field_rgb(EdRect* body, const char* label,
                     unsigned char* r, unsigned char* g, unsigned char* b)
{
    const EdRect box = EdUI_FieldRow(body, label, LABEL_W);
    const float sw = 20.0f;
    EdRect row = box;
    const EdRect chip = EdR_Cut(&row, sw, ED_SIDE_RIGHT);
    EdUI_Plate(EdR_Inset(chip, 2.0f), EDUI_TILE_ROUND3,
               0xFF000000u | ((unsigned)*r << 16) | ((unsigned)*g << 8) | *b);

    const float w = (row.w - 4.0f) / 3.0f;
    int changed = 0;
    unsigned char* ch[3] = { r, g, b };
    const unsigned int tint[3] = { EDC_X, EDC_Y, EDC_Z };
    for (int i = 0; i < 3; i++) {
        EdRect cell = EdR(row.x + i * (w + 2.0f), row.y, w, row.h);
        int t = *ch[i];
        const EdId id = EdUI_IdIdx(label, i);
        if (EdUI_DragInt(id, cell, &t, 0.7f, 0, 255, "")) {
            *ch[i] = (unsigned char)t;
            changed = 1;
        }
        EdUI_Fill(EdR(cell.x + 1.0f, cell.y + cell.h - 2.0f, cell.w - 2.0f, 1.0f),
                  tint[i]);
    }
    return changed;
}

// The X/Y/Z line, with the axis letters coloured the way the gizmo is - the
// one place in the panel where colour carries meaning rather than decoration.
static int field_vec3_short(EdRect* body, const char* label,
                            short* x, short* y, short* z)
{
    const EdRect box = EdUI_FieldRow(body, label, LABEL_W);
    const float w = (box.w - 8.0f) / 3.0f;
    short* v[3] = { x, y, z };
    const unsigned int tint[3] = { EDC_X, EDC_Y, EDC_Z };
    int changed = 0;
    for (int i = 0; i < 3; i++) {
        if (v[i] == NULL) continue;
        EdRect cell = EdR(box.x + i * (w + 4.0f), box.y, w, box.h);
        int t = *v[i];
        const EdId id = EdUI_IdIdx(label, i);
        if (EdUI_DragInt(id, cell, &t, 4.0f, -32000, 32000, "")) {
            *v[i] = (short)t;
            changed = 1;
        }
        EdUI_Fill(EdR(cell.x + 1.0f, cell.y + cell.h - 2.0f, cell.w - 2.0f, 2.0f),
                  tint[i]);
    }
    return changed;
}

static void details_empty(EdRect body)
{
    EdUI_Icon(EdR(body.x + body.w * 0.5f - 22.0f, body.y + 40.0f, 44.0f, 44.0f),
              EDUI_ICON_SELECT, 0x30FFFFFFu);
    EdUI_TextIn(EDUI_FONT_UI, "Nothing selected",
                EdR(body.x, body.y + 92.0f, body.w, 20.0f), ED_ALIGN_CENTRE,
                EDC_TEXT_DIM);
    EdUI_TextIn(EDUI_FONT_SMALL, "Click something in the viewport,",
                EdR(body.x, body.y + 112.0f, body.w, 16.0f), ED_ALIGN_CENTRE,
                EDC_TEXT_FAINT);
    EdUI_TextIn(EDUI_FONT_SMALL, "or a row in the outliner.",
                EdR(body.x, body.y + 126.0f, body.w, 16.0f), ED_ALIGN_CENTRE,
                EDC_TEXT_FAINT);
}

// ---------------------------------------------------------------------------
// The sections, one per kind
// ---------------------------------------------------------------------------
static void details_box(EdRect* page, RaidBox* B)
{
    EditorShell* S = &g_edShell;
    int dirty = 0;

    if (EdUI_Section(EdUI_Id("det.box.t"), page, "Transform", &S->detailOpen[0])) {
        dirty |= field_vec3_short(page, "Min", &B->x0, &B->y0, &B->z0);
        dirty |= field_vec3_short(page, "Max", &B->x1, &B->y1, &B->z1);

        // Size is the useful handle for a wall; editing it moves the far face
        // and leaves the near one where it was put.
        short w = (short)(B->x1 - B->x0);
        short h = (short)(B->y1 - B->y0);
        short d = (short)(B->z1 - B->z0);
        if (field_vec3_short(page, "Size", &w, &h, &d)) {
            B->x1 = (short)(B->x0 + w);
            B->y1 = (short)(B->y0 + h);
            B->z1 = (short)(B->z0 + d);
            dirty = 1;
        }
        EdUI_Separator(page);
    }

    if (EdUI_Section(EdUI_Id("det.box.a"), page, "Appearance", &S->detailOpen[1])) {
        const EdRect sh = EdUI_FieldRow(page, "Shade", LABEL_W);
        EdUI_SliderFloat(EdUI_Id("det.box.shade"), sh, &B->shade, 0.0f, 1.5f,
                         "%.2f");
        dirty |= field_rgb(page, "Tint", &B->tr, &B->tg, &B->tb);

        int draw = (B->flags & RAID_BOX_DRAW) != 0;
        int solid = (B->flags & RAID_BOX_SOLID) != 0;
        int check = (B->flags & RAID_BOX_CHECKER) != 0;
        EdRect row = EdUI_FieldRow(page, "Flags", LABEL_W);
        const float cw = row.w / 3.0f;
        if (EdUI_Check(EdUI_Id("det.box.draw"), EdR(row.x, row.y, cw, row.h),
                       "Draw", &draw)) dirty = 1;
        if (EdUI_Check(EdUI_Id("det.box.solid"),
                       EdR(row.x + cw, row.y, cw, row.h), "Solid", &solid))
            dirty = 1;
        if (EdUI_Check(EdUI_Id("det.box.check"),
                       EdR(row.x + cw * 2.0f, row.y, cw, row.h), "Check", &check))
            dirty = 1;
        B->flags = (unsigned short)((draw ? RAID_BOX_DRAW : 0)
                                  | (solid ? RAID_BOX_SOLID : 0)
                                  | (check ? RAID_BOX_CHECKER : 0));
        EdUI_Separator(page);
    }

    // A box that moved or stopped being solid changes the collision table the
    // player is standing on, and that is built by RaidLevel_Apply.
    if (dirty) RaidLevel_Apply();
}

static void details_item(EdRect* page, RaidItem* I, int index)
{
    EditorShell* S = &g_edShell;
    if (EdUI_Section(EdUI_Id("det.item.t"), page, "Transform", &S->detailOpen[0])) {
        field_vec3_short(page, "Position", &I->x, NULL, &I->z);
        field_angle(page, "Yaw", &I->angle);
        EdUI_Separator(page);
    }
    if (EdUI_Section(EdUI_Id("det.item.p"), page, "Pickup", &S->detailOpen[1])) {
        const EdRect tr = EdUI_FieldRow(page, "Item", LABEL_W);
        char name[64];
        snprintf(name, sizeof(name), "%s", EdContent_ItemName(I->type));
        EdUI_Plate(tr, EDUI_TILE_ROUND3, EDC_FIELD);
        EdUI_TextIn(EDUI_FONT_UI, name,
                    EdR(tr.x + 6.0f, tr.y, tr.w - 12.0f, tr.h), ED_ALIGN_LEFT,
                    EDC_TEXT);

        int type = I->type;
        if (field_int(page, "Type id", &type, 1, 255, 0.4f, "")) {
            I->type = (unsigned char)type;
            RaidItemModels_Sync();
            RaidItems_Reset();
        }
        if (EdContent_ItemStacks(I->type)) {
            field_byte(page, "Amount", &I->amount, 1, 250);
        } else {
            const EdRect a = EdUI_FieldRow(page, "Amount", LABEL_W);
            EdUI_TextIn(EDUI_FONT_SMALL,
                        "one slot - this item does not stack",
                        a, ED_ALIGN_LEFT, EDC_TEXT_FAINT);
        }

        char st[64];
        snprintf(st, sizeof(st), RaidItems_Taken(index) ? "taken this run"
                                                        : "on the floor");
        const EdRect s = EdUI_FieldRow(page, "State", LABEL_W);
        EdUI_TextIn(EDUI_FONT_SMALL, st, s, ED_ALIGN_LEFT, EDC_TEXT_DIM);
        EdUI_Separator(page);
    }
}

static void details_enemy(EdRect* page, RaidEnemy* E)
{
    EditorShell* S = &g_edShell;
    if (EdUI_Section(EdUI_Id("det.enemy.t"), page, "Transform", &S->detailOpen[0])) {
        field_vec3_short(page, "Position", &E->x, NULL, &E->z);
        field_angle(page, "Yaw", &E->angle);
        EdUI_Separator(page);
    }
    if (EdUI_Section(EdUI_Id("det.enemy.k"), page, "Enemy", &S->detailOpen[1])) {
        const EdRect tr = EdUI_FieldRow(page, "Kind", LABEL_W);
        EdUI_Plate(tr, EDUI_TILE_ROUND3, EDC_FIELD);
        EdUI_TextIn(EDUI_FONT_UI, EdContent_EnemyName(E->type),
                    EdR(tr.x + 6.0f, tr.y, tr.w - 12.0f, tr.h), ED_ALIGN_LEFT,
                    EDC_TEXT);
        int type = E->type;
        if (field_int(page, "Type id", &type, 0, g_edEnemyCount - 1, 0.2f, ""))
            E->type = (unsigned char)type;
        const EdRect n = EdUI_FieldRow(page, "", LABEL_W);
        EdUI_TextIn(EDUI_FONT_SMALL, "takes effect on the next Play",
                    n, ED_ALIGN_LEFT, EDC_TEXT_FAINT);
        EdUI_Separator(page);
    }
}

static void details_light(EdRect* page, RaidLight* G)
{
    EditorShell* S = &g_edShell;
    int dirty = 0;
    if (EdUI_Section(EdUI_Id("det.light.t"), page, "Transform", &S->detailOpen[0])) {
        dirty |= field_int(page, "X", &G->x, -32000, 32000, 4.0f, "");
        dirty |= field_int(page, "Y", &G->y, -32000, 32000, 4.0f, "");
        dirty |= field_int(page, "Z", &G->z, -32000, 32000, 4.0f, "");
        EdUI_Separator(page);
    }
    if (EdUI_Section(EdUI_Id("det.light.l"), page, "Light", &S->detailOpen[1])) {
        dirty |= field_rgb(page, "Colour", &G->r, &G->g, &G->b);
        dirty |= field_short(page, "Radius", &G->radius, 100, 30000, "");
        const EdRect n = EdUI_FieldRow(page, "", LABEL_W);
        // Worth saying where it is: the falloff surprises people the first
        // time a light above a character does nothing.
        EdUI_TextIn(EDUI_FONT_SMALL, "falls off in XZ only",
                    n, ED_ALIGN_LEFT, EDC_TEXT_FAINT);
        EdUI_Separator(page);
    }
    if (dirty) RaidLevel_Apply();
}

static void details_cam(EdRect* page, RaidCam* C)
{
    EditorShell* S = &g_edShell;
    int dirty = 0;
    if (EdUI_Section(EdUI_Id("det.cam.t"), page, "Transform", &S->detailOpen[0])) {
        dirty |= field_int(page, "Eye X", &C->fx, -32000, 32000, 4.0f, "");
        dirty |= field_int(page, "Eye Y", &C->fy, -32000, 32000, 4.0f, "");
        dirty |= field_int(page, "Eye Z", &C->fz, -32000, 32000, 4.0f, "");
        dirty |= field_int(page, "Aim X", &C->tx, -32000, 32000, 4.0f, "");
        dirty |= field_int(page, "Aim Y", &C->ty, -32000, 32000, 4.0f, "");
        dirty |= field_int(page, "Aim Z", &C->tz, -32000, 32000, 4.0f, "");
        EdUI_Separator(page);
    }
    if (EdUI_Section(EdUI_Id("det.cam.l"), page, "Lens", &S->detailOpen[1])) {
        dirty |= field_int(page, "Focal", &C->fov, 60, 900, 1.0f, "");
        const EdRect n = EdUI_FieldRow(page, "", LABEL_W);
        EdUI_TextIn(EDUI_FONT_SMALL, "a focal length, not an angle",
                    n, ED_ALIGN_LEFT, EDC_TEXT_FAINT);
        EdUI_Separator(page);
    }
    const EdRect b = EdUI_FieldRow(page, "", LABEL_W);
    if (EdUI_Button(EdUI_Id("det.cam.here"), b, "Set from view", EDUI_ICON_CAMERA, 0))
        EdAct_CameraFromView();
    if (dirty) RaidLevel_Apply();
}

static void details_zone(EdRect* page, RaidZone* Z)
{
    EditorShell* S = &g_edShell;
    if (EdUI_Section(EdUI_Id("det.zone.t"), page, "Rectangle", &S->detailOpen[0])) {
        field_short(page, "X min", &Z->x0, -32000, 32000, "");
        field_short(page, "X max", &Z->x1, -32000, 32000, "");
        field_short(page, "Z min", &Z->z0, -32000, 32000, "");
        field_short(page, "Z max", &Z->z1, -32000, 32000, "");
        field_short(page, "Camera", &Z->cam, 0,
                    (g_raidLevel.ncam > 0 ? g_raidLevel.ncam - 1 : 0), "");
        EdUI_Separator(page);
    }
}

static void details_spawn(EdRect* page)
{
    EditorShell* S = &g_edShell;
    RaidLevel* L = &g_raidLevel;
    if (EdUI_Section(EdUI_Id("det.spawn.t"), page, "Player Start", &S->detailOpen[0])) {
        field_int(page, "X", &L->spawnX, -32000, 32000, 4.0f, "");
        field_int(page, "Z", &L->spawnZ, -32000, 32000, 4.0f, "");
        int deg = ((L->spawnAngle & 0xFFF) * 360) / 4096;
        const EdRect a = EdUI_FieldRow(page, "Yaw", LABEL_W);
        if (EdUI_DragInt(EdUI_Id("det.spawn.yaw"), a, &deg, 0.6f, 0, 359, " deg"))
            L->spawnAngle = (deg * 4096) / 360;
        EdUI_Separator(page);
    }
    if (EdUI_Section(EdUI_Id("det.spawn.i"), page, "Starting Inventory",
                     &S->detailOpen[1])) {
        for (int i = 0; i < L->ngive; i++) {
            char lab[16];
            snprintf(lab, sizeof(lab), "Slot %d", i);
            const EdRect row = EdUI_FieldRow(page, lab, LABEL_W);
            EdRect left = row;
            const EdRect amt = EdR_Cut(&left, 52.0f, ED_SIDE_RIGHT);
            EdUI_Plate(left, EDUI_TILE_ROUND3, EDC_FIELD);
            EdUI_TextIn(EDUI_FONT_SMALL, EdContent_ItemName(L->give[i].type),
                        EdR(left.x + 5.0f, left.y, left.w - 10.0f, left.h),
                        ED_ALIGN_LEFT, EDC_TEXT);
            int n = L->give[i].amount;
            if (EdUI_DragInt(EdUI_IdIdx("det.give", i), amt, &n, 0.7f, 1, 250, ""))
                L->give[i].amount = (unsigned char)n;
        }
        if (L->ngive == 0) {
            const EdRect n = EdUI_FieldRow(page, "", LABEL_W);
            EdUI_TextIn(EDUI_FONT_SMALL, "empty - the mode's built-in loadout",
                        n, ED_ALIGN_LEFT, EDC_TEXT_FAINT);
        }
        EdUI_Separator(page);
    }
}

// ---------------------------------------------------------------------------
void EdPanel_Details(EdRect r)
{
    EdUI_Fill(r, EDC_PANEL);
    EdRect body = EdPanel_Header(r, EDUI_ICON_COG, "Details",
                                 EditorSelect_Valid() ? EditorSelect_Name() : "");
    EdUI_Fill(body, EDC_PANEL);

    if (!EditorSelect_Valid() && g_edSel.kind != ED_SPAWN) {
        details_empty(body);
        return;
    }

    // One page tall enough for the deepest section set; the scroll region
    // clips whatever is not reached.
    EdRect page = EdUI_ScrollBegin(EdUI_Id("details"), body, 460.0f);
    EdRect cur = page;
    cur.w -= 4.0f;              // the colour swatch sits at the right edge

    RaidLevel* L = &g_raidLevel;
    switch (g_edSel.kind) {
    case ED_BOX:   details_box(&cur, &L->box[g_edSel.index]); break;
    case ED_ITEM:  details_item(&cur, &L->item[g_edSel.index], g_edSel.index); break;
    case ED_ENEMY: details_enemy(&cur, &L->enemy[g_edSel.index]); break;
    case ED_LIGHT: details_light(&cur, &L->light[g_edSel.index]); break;
    case ED_CAM:   details_cam(&cur, &L->cam[g_edSel.index]); break;
    case ED_ZONE:  details_zone(&cur, &L->zone[g_edSel.index]); break;
    case ED_SPAWN: details_spawn(&cur); break;
    default: break;
    }

    // The two commands that belong to the thing rather than to the level.
    if (g_edSel.kind != ED_SPAWN) {
        EdRect row = EdUI_FieldRow(&cur, "", 0.0f);
        const float w = (row.w - EDM_GAP) * 0.5f;
        if (EdUI_Button(EdUI_Id("det.dup"), EdR(row.x, row.y, w, row.h),
                        "Duplicate", EDUI_ICON_PLUS, 0)) EdAct_Duplicate();
        if (EdUI_Button(EdUI_Id("det.del"),
                        EdR(row.x + w + EDM_GAP, row.y, w, row.h),
                        "Delete", EDUI_ICON_TRASH, 0)) EdAct_Delete();
    }

    EdUI_ScrollEnd();
}
