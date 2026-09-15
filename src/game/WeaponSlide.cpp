// WeaponSlide.cpp - the Beretta's slide, moving.
//
// CUSTOM. Not in the original game.
//
// The in-hand weapon model (players/W12.EMW) is the gloved fist and the pistol
// modelled as ONE mesh, and the pistol half of it is not a pistol at all - it
// is a plain eight-vertex box, because the grip is hidden inside the fist and
// only the SLIDE is ever visible. That is the whole reason this is cheap: the
// part that should move on every shot is already the only part that is
// modelled, sitting in its own connected component.
//
// So the slide is moved where it lives - in the vertex array the renderer
// reads. The one thing that DID have to be authored is a barrel for it to
// uncover (tools/build_beretta_barrel.py): a featureless box sliding backwards
// reads as the gun getting shorter, not as a slide going back. That box stays
// put; this file never touches it.
//
// WHY WRITING VERTICES IS LEGAL HERE
//
// A TMD is parsed ONCE, by PSXObject_Store, into a plain heap array of 11-float
// vertices hanging off a CMarniViewport2 (m_pVertexBuffer). After that
// CreateTmdObjectInternal short-circuits on a cached slot and the TMD bytes are
// never read again; FlushTmdObjects transforms and projects straight out of
// that array on the CPU, every frame. There is no GPU vertex buffer, so there
// is nothing to invalidate: write a position and the next frame draws it.
//
// WHERE IT HAS TO RUN
//
// In the render flush, immediately before FlushTmdObjects - not in
// update_player_anim, which is where this was first put and where it did
// nothing at all. PSXObject_Store is the only thing that writes a parsed TMD's
// vertex array, and render_entity can re-run it for a model whose cached slot
// in g_objectDeletePtr has been reclaimed by another entity. That restores
// every vertex from the file, wiping the edit a few hundred instructions after
// it was made - the readout showed the array holding the displaced values while
// the screen showed a gun that never moved. Running last closes the window:
// everything is parsed and queued by then, and the flush reads the array on the
// next line.
//
// TWO THINGS THE PARSE DOES THAT MATTER
//
//   * It EXPANDS. Each triangle gets three fresh vertices, so primitive p owns
//     buffer vertices 3p, 3p+1, 3p+2 and the source's shared corners exist
//     several times over. The slide is 12 triangles = 36 vertices, not 8.
//   * It NEGATES Y (PSXObjReadVertex: `v->y = -(float)p[1]`). The model's +Y is
//     the muzzle direction, so in the buffer the muzzle is -Y and a slide
//     travelling REARWARDS is a POSITIVE y offset.
#include "../Globals.h"
#include "Types.h"
#include "Entities.h"
#include "../marni/Marni3DObject.h"
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// Which vertices are the slide.
//
// Measured off the shipped W12.EMW rather than assumed. Its TMD is a single
// packet kind (0x34000609) whose weapon half splits by shared vertices into:
//
//   fist   34 tris, verts 0-18,  model X -62..48  Y   1..262  Z -50..69
//   slide  12 tris, verts 19-26, model X  47..127 Y  84..424  Z -27..27
//
// In BUFFER space (Y negated) the test below selects those twelve triangles and
// no others. Each clause earns its place: X alone leaves fist vertices 2 and 3
// in (they reach 47 and 48), and they are excluded by Z, which the slide holds
// to +/-27 while the fist splays to -50..69. Y alone separates nothing, because
// the fist runs the length of the grip.
//
// It is applied PER TRIANGLE - all three corners or none - so a near miss can
// never tear one triangle away from the mesh, and the result is checked against
// the expected count before anything is moved.
// ---------------------------------------------------------------------------
// The test is on |Y|, not Y, because whether the parse negates Y is a property
// of the loader and not something this file should assert. Nothing else needs
// the sign: the slide is the only part of the mesh that is far along the barrel
// AND narrow in Z, whichever way the axis points. The direction it travels is
// then derived from the measured data (s_dir), not assumed.
// The lower Z bound is what keeps the BARREL out of the selection. The model
// now carries one (tools/build_beretta_barrel.py): a thinner box running
// through the slide and a few units past the muzzle, which is what the slide
// going back uncovers. It has to stay put, and it is the narrow one - the
// slide holds +/-27 and the barrel +/-12.
#define SLIDE_MIN_X     47.0f
#define SLIDE_MIN_ABS_Y 80.0f
#define SLIDE_MIN_ABS_Z 18.0f
#define SLIDE_MAX_ABS_Z 32.0f

#define SLIDE_EXPECT_TRIS   12
#define SLIDE_MAX_VERTS     64

// How far back it travels, in model units. A real Beretta's slide moves about
// a seventh of its own length; this is nearly a third, on purpose. The whole
// gun is about fifty pixels across in a 320x240 frame and the stroke lasts a
// tenth of a second under a muzzle flash, so a physically honest travel is a
// travel nobody sees. Readability wins here.
#define SLIDE_TRAVEL    150.0f

// Frames of the cycle after a shot. The fire motion is eight frames long, and
// the muzzle smoke sits over the weapon for most of them - so the stroke is
// held open for three frames and closes over three more, which puts the
// closing half AFTER the smoke has started to thin instead of inside it.
#define SLIDE_CYCLE     6

// Once the magazine runs dry the slide is held back, and it keeps being held
// for this long after the count comes back up. Both halves matter:
//
//   * while the count IS zero the hold is re-armed every frame, so it lasts
//     exactly as long as the gun is dry - that part is open-ended;
//   * the run-down afterwards is what makes it SEEN. Firing the last round with
//     ammo in the inventory drops the player into behaviour 0x18, which refills
//     the magazine within a frame or two, so "while ammo == 0" on its own is a
//     state that exists and flashes past. The slide therefore stays back
//     through the reload and only runs forward at the end of it, which is also
//     where it belongs: that forward stroke IS the chambering.
#define SLIDE_EMPTY_HOLD 26

// ---------------------------------------------------------------------------
// Cached resolution. Everything is re-derived whenever the vertex array moves,
// which is what a weapon change or a model reload does.
// ---------------------------------------------------------------------------
static float* s_vb        = NULL;    // the element's vertex array
static void*  s_slot      = NULL;    // the CMarniDirect3DTMD it belongs to
static int    s_elem      = 0;       // which element of it
static int    s_vtx       = 0;       // its vertex count when measured
static float  s_chkX      = 0.0f;    // a coordinate this code never writes,
static float  s_chkZ      = 0.0f;    // kept to recognise the same mesh again
static float  s_dir       = 1.0f;    // sign that moves the slide rearwards
static int    s_count     = 0;       // how many entries of s_idx are live
static int    s_idx[SLIDE_MAX_VERTS];
static float  s_origY[SLIDE_MAX_VERTS];

static int    s_cycle     = 0;       // frames left of the post-shot cycle
static int    s_empty     = 0;       // frames left of the locked-back hold
static int    s_prevAmmo  = -1;
static float  s_applied   = 0.0f;    // the offset currently written into the array

// The stock Beretta only. The three custom pistols alias equippedWeaponId to
// ITEM_BERETTA - that alias is what makes the aim/fire state machine work - so
// the real id has to come from the equipped inventory slot. Their models are
// their own meshes with their own geometry, and the test above means nothing
// there; this keeps the effect off them rather than relying on that.
static int slide_is_stock_beretta(void)
{
    if (g_EquippedItemId == 0) return 0;
    if (g_playerEntity.equippedWeaponId != ITEM_BERETTA) return 0;
    return ((unsigned char*)g_ItemSlotsPointer)[(g_EquippedItemId - 1) * 2] == ITEM_BERETTA;
}

static int slide_ammo(void)
{
    if (g_EquippedItemId == 0) return 0;
    return ((unsigned char*)g_ItemSlotsPointer)[g_EquippedItemId * 2 - 1] & 0x7f;
}

// Drop the cache. ALWAYS puts the slide back first: letting go of a retracted
// slide is how it gets stuck open - the next parse of this model would keep the
// displaced positions as its own rest pose and every later cycle would stack on
// top of them.
static void slide_forget(void)
{
    if (s_vb != NULL && s_count != 0 && s_applied != 0.0f) {
        for (int i = 0; i < s_count; i++) {
            s_vb[s_idx[i] * 11 + 1] = s_origY[i];
        }
    }
    s_vb = NULL;
    s_slot = NULL;
    s_count = 0;
    s_applied = 0.0f;
}

// Find the slide's vertices in the in-hand weapon's element. Returns 1 when
// s_vb / s_idx / s_origY are usable.
static int slide_resolve(void)
{
    // Joint 14 is the weapon/hand joint; CreateAnimObject parks the
    // CMarniDirect3DTMD slot pointer in word 8 of its anim object. Read it off
    // the joint rather than off g_animObjectBuffer - that IS what every live
    // call site passes, but the joint is where the loader actually puts it.
    // Null until the first draw of that joint has run, so the very first frame
    // after a weapon change simply finds nothing.
    // Joint 14 is the weapon/hand joint. Read its anim_object rather than
    // assuming g_animObjectBuffer: that IS what every live call site passes,
    // but the joint is where the loader actually parks it, so this cannot
    // drift if a call site ever changes. CreateAnimObject leaves the
    // CMarniDirect3DTMD slot pointer in word 8 of it.
    const DWORD* spriteData = (const DWORD*)g_playerEntity.jointsStructs[0x0e].anim_object;
    if (spriteData == NULL) { slide_forget(); return 0; }

    void* slot = (void*)(size_t)spriteData[8];
    if (slot == NULL) { slide_forget(); return 0; }

    const int nElem = (int)*(DWORD*)((BYTE*)slot + 0x4C0);
    if (nElem <= 0 || nElem > 16) { slide_forget(); return 0; }

    // Already resolved against this model? The slot pointer is not enough to
    // say so: re-equipping a weapon re-runs PSXObject_Store into the SAME slot,
    // which frees and reallocates the vertex array. Check the array and its
    // length, and that a coordinate this code never touches still reads back -
    // then a rebuilt or replaced mesh forces a fresh resolve instead of being
    // written through a stale pointer.
    if (s_slot == slot && s_vb != NULL && s_count != 0) {
        CMarniViewport2* e = (CMarniViewport2*)((BYTE*)slot + s_elem * 0x4C);
        if ((float*)e->m_pVertexBuffer == s_vb
            && (int)e->m_vertexCount == s_vtx
            && s_vb[s_idx[0] * 11 + 0] == s_chkX
            && s_vb[s_idx[0] * 11 + 2] == s_chkZ) {
            return 1;
        }
        // Whatever is there now is not what was measured. Do not restore -
        // those positions belong to a mesh that no longer exists.
        s_vb = NULL; s_slot = NULL; s_count = 0; s_applied = 0.0f;
    }

    for (int k = 0; k < nElem; k++) {
        CMarniViewport2* e = (CMarniViewport2*)((BYTE*)slot + k * 0x4C);
        float* vb = (float*)e->m_pVertexBuffer;
        const int vtxCount = (int)e->m_vertexCount;
        if (vb == NULL || vtxCount < 3) continue;
        if (e->m_primitiveType != 3) continue;      // triangles only

        int idx[SLIDE_MAX_VERTS];
        int n = 0;
        int tris = 0;
        for (int v = 0; v + 2 < vtxCount; v += 3) {
            int ok = 1;
            for (int c = 0; c < 3; c++) {
                const float* p = vb + (v + c) * 11;
                const float ay = (p[1] < 0.0f) ? -p[1] : p[1];
                const float az = (p[2] < 0.0f) ? -p[2] : p[2];
                if (!(p[0] >= SLIDE_MIN_X && ay >= SLIDE_MIN_ABS_Y
                      && az >= SLIDE_MIN_ABS_Z && az <= SLIDE_MAX_ABS_Z)) {
                    ok = 0;
                    break;
                }
            }
            if (!ok) continue;
            if (n + 3 > SLIDE_MAX_VERTS) { n = 0; break; }
            idx[n++] = v;
            idx[n++] = v + 1;
            idx[n++] = v + 2;
            tris++;
        }

        // The count is the safety net. If the mesh is ever re-authored and the
        // test stops picking out exactly the slide, the effect turns itself off
        // instead of dragging part of the hand around.
        if (tris != SLIDE_EXPECT_TRIS) continue;

        // And the rest pose has to look like a slide at rest. A mesh whose
        // slide is already pushed back would be measured as though that were
        // where it lives, and every cycle after would start from there.
        float lo = vb[idx[0] * 11 + 1], hi = lo;
        for (int i = 1; i < n; i++) {
            const float y = vb[idx[i] * 11 + 1];
            if (y < lo) lo = y;
            if (y > hi) hi = y;
        }
        const float alo = (lo < 0.0f) ? -lo : lo;
        const float ahi = (hi < 0.0f) ? -hi : hi;
        const float span = (ahi > alo) ? (ahi - alo) : (alo - ahi);
        if (span < 280.0f) continue;

        // Which way is rearwards. The muzzle is the end furthest from the
        // grip, so it is the extreme with the larger magnitude; the slide
        // travels back toward zero, i.e. AGAINST that end's sign.
        const float muzzle = (ahi > alo) ? hi : lo;
        s_dir = (muzzle < 0.0f) ? 1.0f : -1.0f;

        s_vb = vb;
        s_slot = slot;
        s_elem = k;
        s_vtx = vtxCount;
        s_count = n;
        for (int i = 0; i < n; i++) {
            s_idx[i] = idx[i];
            s_origY[i] = vb[idx[i] * 11 + 1];
        }
        s_chkX = vb[idx[0] * 11 + 0];
        s_chkZ = vb[idx[0] * 11 + 2];
        s_applied = 0.0f;
        return 1;
    }

    slide_forget();
    return 0;
}

static void slide_write(float travel)
{
    if (s_vb == NULL || s_count == 0) return;
    if (travel == s_applied) return;            // nothing moved this frame
    for (int i = 0; i < s_count; i++) {
        s_vb[s_idx[i] * 11 + 1] = s_origY[i] + travel * s_dir;
    }
    s_applied = travel;
}

// ===========================================================================
// WeaponSlide_Update - once per frame, from the render flush.
// ===========================================================================
void WeaponSlide_Update(void)
{
    if (!slide_is_stock_beretta()) {
        // Put it back before letting go, or the next time this model is drawn
        // it is drawn with the slide still open.
        if (s_vb != NULL) slide_write(0.0f);
        slide_forget();
        s_prevAmmo = -1;
        s_cycle = 0;
        return;
    }

    if (!slide_resolve()) {
        s_prevAmmo = -1;
        return;
    }

    const int ammo = slide_ammo();

    // A shot is a round leaving the magazine. Watching the count rather than
    // the animation frame is what makes this work on every path that fires -
    // the single shot, the auto-aim fire and the held-fire loop all decrement
    // it, and none of them share a frame number.
    if (s_prevAmmo >= 0 && ammo < s_prevAmmo) {
        s_cycle = SLIDE_CYCLE;
    }
    s_prevAmmo = ammo;

    // Arm the hold the moment the magazine empties, and let it run down on its
    // own. Tying it to `ammo == 0` alone loses the whole effect to the
    // auto-reload; this way the slide is still back while she reloads, which is
    // when it should be.
    if (ammo == 0) {
        s_empty = SLIDE_EMPTY_HOLD;
    } else if (s_empty > 0) {
        s_empty--;
        if (s_empty == 0) {
            // The slide has just seated. That is a mechanical event with a
            // sound of its own - the round being chambered - and it is the beat
            // that tells the player the gun is live again. Same bank and cue as
            // the dry click, which is the weapon's own metal-on-metal sample.
            Play3DSnd(1, 9, 0, (int)&g_playerEntity.scaMatrixData.localMatrix.t);
        }
    }

    float travel;
    if (s_empty > 0) {
        // Locked back: the one piece of state the player can read off the
        // weapon itself instead of the inventory. The last four frames of the
        // hold are the slide running home - a jump from fully back to fully
        // forward in one frame reads as a glitch, not as chambering - and the
        // click lands on the frame it seats (see the s_empty run-down above).
        travel = (s_empty <= 4)
               ? SLIDE_TRAVEL * ((float)s_empty * 0.25f)
               : SLIDE_TRAVEL;
        s_cycle = 0;
    } else if (s_cycle > 0) {
        // Snap open, ease closed: three frames fully back, then the return.
        static const float k[SLIDE_CYCLE + 1] =
            { 0.0f, 0.20f, 0.45f, 0.75f, 1.0f, 1.0f, 1.0f };
        travel = SLIDE_TRAVEL * k[s_cycle];
        s_cycle--;
    } else {
        travel = 0.0f;
    }

    slide_write(travel);
}
