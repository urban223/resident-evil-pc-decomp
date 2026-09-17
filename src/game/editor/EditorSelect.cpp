// EditorSelect.cpp - what the cursor is pointing at.
//
// CUSTOM.
//
// Everything in a RAID level is either a box or a point, so one ray/AABB test
// covers the lot: a box is its own extent, and a point gets an extent big
// enough to click. The nearest hit along the ray wins, which is the only rule
// that behaves the way a 3D view looks.
//
// A selection is a kind and an INDEX, never a pointer. F7 rebuilds every array
// in g_raidLevel wholesale, and a pointer into the old one is a crash waiting
// for the next reload.
#include "EditorState.h"
#include "../../Globals.h"
#include "../Types.h"
#include "../RaidLevel.h"
#include <cmath>
#include <cstdio>

EditorSel g_edSel = { ED_NONE, 0 };

// How big a point has to be to be clickable. Roughly a body for the people,
// smaller for the things that are only a marker.
#define ED_PICK_PERSON   320.0f
#define ED_PICK_THING    240.0f
#define ED_ITEM_HOVER    620.0f

void EditorSelect_Clear(void)
{
    g_edSel.kind = ED_NONE;
    g_edSel.index = 0;
}

int EditorSelect_Valid(void)
{
    if (!g_raidLevel.loaded) return 0;
    switch (g_edSel.kind) {
        case ED_BOX:   return g_edSel.index < g_raidLevel.nbox;
        case ED_ITEM:  return g_edSel.index < g_raidLevel.nitem;
        case ED_ENEMY: return g_edSel.index < g_raidLevel.nenemy;
        case ED_LIGHT: return g_edSel.index < g_raidLevel.nlight;
        case ED_CAM:   return g_edSel.index < g_raidLevel.ncam;
        case ED_ZONE:  return g_edSel.index < g_raidLevel.nzone;
        case ED_SPAWN: return 1;
        default:       return 0;
    }
}

// ---------------------------------------------------------------------------
// One candidate's box. Returns 0 when the kind/index does not exist.
// ---------------------------------------------------------------------------
static int ed_bounds_of(int kind, int i,
                        float* x0, float* y0, float* z0,
                        float* x1, float* y1, float* z1)
{
    const RaidLevel* L = &g_raidLevel;
    float cx, cy, cz, h;

    switch (kind) {
    case ED_BOX: {
        if (i >= L->nbox) return 0;
        const RaidBox* B = &L->box[i];
        *x0 = (float)B->x0; *x1 = (float)B->x1;
        *z0 = (float)B->z0; *z1 = (float)B->z1;
        // A plate has no thickness, and a zero-height slab is something the
        // cursor can never quite be inside. Give it a little.
        *y0 = (float)B->y0 - (B->y0 == B->y1 ? 8.0f : 0.0f);
        *y1 = (float)B->y1 + (B->y0 == B->y1 ? 8.0f : 0.0f);
        return 1;
    }
    case ED_ITEM:
        if (i >= L->nitem) return 0;
        cx = (float)L->item[i].x; cz = (float)L->item[i].z;
        cy = -ED_ITEM_HOVER; h = ED_PICK_THING;
        break;
    case ED_ENEMY:
        if (i >= L->nenemy) return 0;
        cx = (float)L->enemy[i].x; cz = (float)L->enemy[i].z;
        cy = -1400.0f; h = ED_PICK_PERSON;
        break;
    case ED_SPAWN:
        cx = (float)L->spawnX; cz = (float)L->spawnZ;
        cy = -1400.0f; h = ED_PICK_PERSON;
        break;
    case ED_LIGHT:
        if (i >= L->nlight) return 0;
        cx = (float)L->light[i].x; cz = (float)L->light[i].z;
        cy = (float)L->light[i].y; h = ED_PICK_THING;
        break;
    case ED_CAM:
        if (i >= L->ncam) return 0;
        cx = (float)L->cam[i].fx; cz = (float)L->cam[i].fz;
        cy = (float)L->cam[i].fy; h = ED_PICK_THING;
        break;
    case ED_ZONE: {
        if (i >= L->nzone) return 0;
        const RaidZone* Z = &L->zone[i];
        *x0 = (float)(Z->x0 < Z->x1 ? Z->x0 : Z->x1);
        *x1 = (float)(Z->x0 < Z->x1 ? Z->x1 : Z->x0);
        *z0 = (float)(Z->z0 < Z->z1 ? Z->z0 : Z->z1);
        *z1 = (float)(Z->z0 < Z->z1 ? Z->z1 : Z->z0);
        *y0 = -260.0f; *y1 = 0.0f;
        return 1;
    }
    default:
        return 0;
    }

    *x0 = cx - h; *x1 = cx + h;
    *y0 = cy - h; *y1 = cy + h;
    *z0 = cz - h; *z1 = cz + h;
    return 1;
}

void EditorSelect_Bounds(float* x0, float* y0, float* z0,
                         float* x1, float* y1, float* z1)
{
    if (!EditorSelect_Valid() ||
        !ed_bounds_of(g_edSel.kind, g_edSel.index, x0, y0, z0, x1, y1, z1)) {
        *x0 = *y0 = *z0 = 0.0f;
        *x1 = *y1 = *z1 = 0.0f;
    }
}

void EditorSelect_Origin(float* x, float* y, float* z)
{
    float a, b, c, d, e, f;
    EditorSelect_Bounds(&a, &b, &c, &d, &e, &f);
    *x = (a + d) * 0.5f;
    *y = (b + e) * 0.5f;
    *z = (c + f) * 0.5f;
}

// ---------------------------------------------------------------------------
// Slab test. Returns the entry distance along the ray, or -1.
// ---------------------------------------------------------------------------
static float ed_ray_box(const float* o, const float* d,
                        float x0, float y0, float z0,
                        float x1, float y1, float z1)
{
    const float lo[3] = { x0, y0, z0 };
    const float hi[3] = { x1, y1, z1 };
    float t0 = 0.0f, t1 = 1.0e12f;

    for (int k = 0; k < 3; k++) {
        if (fabsf(d[k]) < 1.0e-9f) {
            if (o[k] < lo[k] || o[k] > hi[k]) return -1.0f;
            continue;
        }
        float a = (lo[k] - o[k]) / d[k];
        float b = (hi[k] - o[k]) / d[k];
        if (a > b) { const float t = a; a = b; b = t; }
        if (a > t0) t0 = a;
        if (b < t1) t1 = b;
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

void EditorSelect_PickAt(float gameX, float gameY)
{
    if (!g_raidLevel.loaded) return;

    float o[3], d[3];
    EditorView_Ray(gameX, gameY, &o[0], &o[1], &o[2], &d[0], &d[1], &d[2]);

    int bestKind = ED_NONE, bestIdx = 0;
    float bestT = 1.0e12f;

    // Order matters only for ties: the small, deliberate things are tested
    // first so a pickup lying on a floor plate is not swallowed by it.
    const struct { int kind; int count; } kinds[] = {
        { ED_SPAWN, 1 },
        { ED_ITEM,  g_raidLevel.nitem },
        { ED_ENEMY, g_raidLevel.nenemy },
        { ED_LIGHT, g_raidLevel.nlight },
        { ED_CAM,   g_raidLevel.ncam },
        { ED_ZONE,  g_raidLevel.nzone },
        { ED_BOX,   g_raidLevel.nbox },
    };

    for (unsigned int k = 0; k < sizeof(kinds)/sizeof(kinds[0]); k++) {
        for (int i = 0; i < kinds[k].count; i++) {
            float x0, y0, z0, x1, y1, z1;
            if (!ed_bounds_of(kinds[k].kind, i, &x0, &y0, &z0, &x1, &y1, &z1)) continue;
            const float t = ed_ray_box(o, d, x0, y0, z0, x1, y1, z1);
            if (t < 0.0f || t >= bestT) continue;
            bestT = t; bestKind = kinds[k].kind; bestIdx = i;
        }
    }

    g_edSel.kind = bestKind;
    g_edSel.index = bestIdx;
}

// ---------------------------------------------------------------------------
// Moving and turning. Only what the format can express: a box has no angle, a
// point has no size.
// ---------------------------------------------------------------------------
void EditorSelect_Move(int dx, int dy, int dz)
{
    if (!EditorSelect_Valid()) return;
    RaidLevel* L = &g_raidLevel;

    switch (g_edSel.kind) {
    case ED_BOX: {
        RaidBox* B = &L->box[g_edSel.index];
        B->x0 = (short)(B->x0 + dx); B->x1 = (short)(B->x1 + dx);
        B->y0 = (short)(B->y0 + dy); B->y1 = (short)(B->y1 + dy);
        B->z0 = (short)(B->z0 + dz); B->z1 = (short)(B->z1 + dz);
        break;
    }
    case ED_ZONE: {
        RaidZone* Z = &L->zone[g_edSel.index];
        Z->x0 = (short)(Z->x0 + dx); Z->x1 = (short)(Z->x1 + dx);
        Z->z0 = (short)(Z->z0 + dz); Z->z1 = (short)(Z->z1 + dz);
        break;
    }
    case ED_ITEM:
        L->item[g_edSel.index].x = (short)(L->item[g_edSel.index].x + dx);
        L->item[g_edSel.index].z = (short)(L->item[g_edSel.index].z + dz);
        break;
    case ED_ENEMY:
        L->enemy[g_edSel.index].x = (short)(L->enemy[g_edSel.index].x + dx);
        L->enemy[g_edSel.index].z = (short)(L->enemy[g_edSel.index].z + dz);
        break;
    case ED_SPAWN:
        L->spawnX += dx;
        L->spawnZ += dz;
        break;
    case ED_LIGHT:
        L->light[g_edSel.index].x += dx;
        L->light[g_edSel.index].y += dy;
        L->light[g_edSel.index].z += dz;
        break;
    case ED_CAM:
        // The whole camera, eye and target together - moving only the eye
        // would swing the shot instead of relocating it.
        L->cam[g_edSel.index].fx += dx; L->cam[g_edSel.index].tx += dx;
        L->cam[g_edSel.index].fy += dy; L->cam[g_edSel.index].ty += dy;
        L->cam[g_edSel.index].fz += dz; L->cam[g_edSel.index].tz += dz;
        break;
    default:
        break;
    }
}

void EditorSelect_Turn(int delta)
{
    if (!EditorSelect_Valid()) return;
    RaidLevel* L = &g_raidLevel;

    switch (g_edSel.kind) {
    case ED_ITEM:
        L->item[g_edSel.index].angle = (short)((L->item[g_edSel.index].angle + delta) & 0xFFF);
        break;
    case ED_ENEMY:
        L->enemy[g_edSel.index].angle = (short)((L->enemy[g_edSel.index].angle + delta) & 0xFFF);
        break;
    case ED_SPAWN:
        L->spawnAngle = (L->spawnAngle + delta) & 0xFFF;
        break;
    case ED_CAM: {
        // Swing the look-at around the eye: the camera turns, it does not move.
        RaidCam* C = &L->cam[g_edSel.index];
        const float t = (float)delta / 4096.0f * 6.2831853f;
        const float c = cosf(t), s = sinf(t);
        const float rx = (float)(C->tx - C->fx), rz = (float)(C->tz - C->fz);
        C->tx = C->fx + (int)(rx*c - rz*s);
        C->tz = C->fz + (int)(rx*s + rz*c);
        break;
    }
    default:
        break;
    }
}

void EditorSelect_Delete(void)
{
    if (!EditorSelect_Valid()) return;
    RaidLevel* L = &g_raidLevel;
    const int i = g_edSel.index;

    // The spawn cannot be deleted - a level without one has nowhere to stand.
    switch (g_edSel.kind) {
    case ED_BOX:
        for (int k = i; k + 1 < L->nbox; k++) L->box[k] = L->box[k+1];
        if (L->nbox > 0) L->nbox--;
        break;
    case ED_ITEM:
        for (int k = i; k + 1 < L->nitem; k++) L->item[k] = L->item[k+1];
        if (L->nitem > 0) L->nitem--;
        break;
    case ED_ENEMY:
        for (int k = i; k + 1 < L->nenemy; k++) L->enemy[k] = L->enemy[k+1];
        if (L->nenemy > 0) L->nenemy--;
        break;
    case ED_LIGHT:
        for (int k = i; k + 1 < L->nlight; k++) L->light[k] = L->light[k+1];
        if (L->nlight > 0) L->nlight--;
        break;
    case ED_ZONE:
        for (int k = i; k + 1 < L->nzone; k++) L->zone[k] = L->zone[k+1];
        if (L->nzone > 0) L->nzone--;
        break;
    case ED_CAM:
        // Not the last one: RaidLevel_Load rejects a level with no camera, and
        // Room_SetupCamera would index past the end of the table.
        if (L->ncam <= 1) return;
        for (int k = i; k + 1 < L->ncam; k++) L->cam[k] = L->cam[k+1];
        L->ncam--;
        break;
    default:
        return;
    }
    EditorSelect_Clear();
}

const char* EditorSelect_Name(void)
{
    static char buf[64];
    if (!EditorSelect_Valid()) return "nothing selected";
    switch (g_edSel.kind) {
    case ED_BOX:   sprintf(buf, "box %d", g_edSel.index); break;
    case ED_ITEM:  sprintf(buf, "item %d  type %d", g_edSel.index,
                           g_raidLevel.item[g_edSel.index].type); break;
    case ED_ENEMY: sprintf(buf, "enemy %d", g_edSel.index); break;
    case ED_SPAWN: sprintf(buf, "spawn"); break;
    case ED_LIGHT: sprintf(buf, "light %d", g_edSel.index); break;
    case ED_CAM:   sprintf(buf, "camera %d", g_edSel.index); break;
    case ED_ZONE:  sprintf(buf, "camzone %d", g_edSel.index); break;
    default:       sprintf(buf, "?"); break;
    }
    return buf;
}

// ---------------------------------------------------------------------------
// The white box round what is selected.
// ---------------------------------------------------------------------------
void EditorSelect_Draw(void)
{
    if (!EditorSelect_Valid()) return;
    float x0, y0, z0, x1, y1, z1;
    EditorSelect_Bounds(&x0, &y0, &z0, &x1, &y1, &z1);

    const unsigned int C = 0xFFFFFFFF;
    const float w = 1.6f;
    // Twelve edges.
    EditorDraw_WorldLine(x0,y0,z0, x1,y0,z0, w, C);
    EditorDraw_WorldLine(x1,y0,z0, x1,y0,z1, w, C);
    EditorDraw_WorldLine(x1,y0,z1, x0,y0,z1, w, C);
    EditorDraw_WorldLine(x0,y0,z1, x0,y0,z0, w, C);
    EditorDraw_WorldLine(x0,y1,z0, x1,y1,z0, w, C);
    EditorDraw_WorldLine(x1,y1,z0, x1,y1,z1, w, C);
    EditorDraw_WorldLine(x1,y1,z1, x0,y1,z1, w, C);
    EditorDraw_WorldLine(x0,y1,z1, x0,y1,z0, w, C);
    EditorDraw_WorldLine(x0,y0,z0, x0,y1,z0, w, C);
    EditorDraw_WorldLine(x1,y0,z0, x1,y1,z0, w, C);
    EditorDraw_WorldLine(x1,y0,z1, x1,y1,z1, w, C);
    EditorDraw_WorldLine(x0,y0,z1, x0,y1,z1, w, C);
}
