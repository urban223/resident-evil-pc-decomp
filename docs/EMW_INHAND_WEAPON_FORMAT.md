# In-hand weapon models (`players/*.emw`) — format reference

How to give an item its own model in the player's hands. The TMD core (packet
layout, winding, normals, index rules) is the same as for item-examine models
and is documented in `docs/IVM_MODEL_FORMAT.md`; this covers only what is
different. Written while giving the flare pistol an in-hand model (Round 8 of
`docs/GRENADE_PISTOL.md`).

## The mesh is the FIST AND the weapon together

This is the thing to know before touching anything else. The TMD inside an
`.emw` is not the weapon — it is the character's gloved fist *and* the weapon
modelled as one mesh. Replace the whole TMD and the hand disappears: it looks
fine while aiming, because the weapon covers the gap, and then the hand is
visibly missing as soon as the arm moves.

The two parts are **separate connected components** — they share no vertex — so
splitting them is exact:

```python
# union-find over triangles that share a vertex index
comps = connected_components(tris)     # for the STOCK w12.emw: exactly 2
hand, weapon = sorted(comps, key=len, reverse=True)   # 34 tris, 12 tris
```

Do **not** try to split them by texture colour. Jill's gloves are black, so
picking "skin-coloured" triangles finds only the 8 exposed-skin ones out of 34
and the rest of the fist gets deleted with the weapon, leaving a few shreds of
skin floating next to the gun.

The **stock** Jill Beretta (`players/w12.emw`) splits as:

| part | tris | verts | bbox |
|---|---|---|---|
| fist | 34 | 0-18 | X -62..48, Y 1..262, Z -50..69, centre (7.9, 146.8, -1.9) |
| weapon | 12 | 19-26 | X 47..127, Y 84..424, Z -27..27 |

Do not expect the file in the tree to match that table any more:
`tools/build_beretta_barrel.py` patches `W12.EMW` in place and it now carries 58
prims and 35 verts in three components, the third being the barrel it adds. The
two-component split is what the tool starts from and asserts against.

Note how crude the weapon half is: a plain box. It is only the slide — the grip
is not modelled at all, because the fist covers it. That is also what makes
`src/game/WeaponSlide.cpp` cheap: the only part that should move on every shot
is already the only part that is modelled, in its own connected component.

## File layout and the size cap

```
[ animation data ][ TMD ][ u32 anim_off ][ u32 tmd_off ]
```

The last two dwords are the trailer, and `LoadEquippedWeaponAnimation` reads
them as `(fileSize & ~3) - 8`. For `w12.emw`: anim 0..22848, TMD at 23812,
trailer at the end. Building a variant is therefore just
`orig[:tmd_off] + new_tmd + struct.pack("<2I", anim_off, tmd_off)` — the
animation data is copied verbatim, so every pose stays exactly as it was.

The file is read into **`g_animationBuffer[37888]`**, another fixed global with
no bounds check (see the `.ivm` doc for the same trap on the item viewer, where
overrunning it corrupted unrelated globals and crashed elsewhere). Keep the
whole file well under that: the flare pistol's `w1f.emw` is 31116 bytes, which
leaves 6772 to spare.

## The weapon has no texture of its own

`LoadEquippedWeaponAnimation` calls `ProcessTmdTextures(2, tmd, 0x16, 7)`, which
walks every textured primitive and does only this:

```c
puVar2[2] += 0x16 * 0x10000;    // tsb  += 0x16
puVar2[1] += 7 * 0x400000;      // cba  += 0x1C0
```

It adds to whatever the file already holds and never touches the u/v bytes. So
author the same base values the original uses (`cba = 30720`, `tsb = 128`) and
the model lands on the same page the stock weapon did — which is the
**character's** texture page (`char11.emd`), not a texture of its own.

That means new UVs must hit texels that already exist on that page. Two
consequences:

- The page is 256 texels wide but addressed as two 128-wide texture pages: `u`
  is always 0..127 and the low nibble of `tsb` picks the half. `char11` itself
  uses both 128 and 129, so both are safe. A texel at global u158 is
  `tsb = 129, u = 30`.
- Jill's page is 242/256 tiles full, so there is nowhere to paint a new colour
  without editing a ROM asset. Instead, search her page for large flat blocks
  and borrow them. Two that work well:

| use | global u,v | tsb, u | colour |
|---|---|---|---|
| orange body | 149-168, 112-132 | 129, 21-40 | rgb(180, 98, 74) terracotta |
| dark parts | 42-53, 92-143 | 128, 42-53 | rgb(8, 24, 16) near-black |

Both are zero-variance runs, so the flat-colour trick (all three corners on one
texel) gives a clean solid colour.

If the page has no usable colour at all, the way out is an **untextured**
primitive kind — `0x30000406`, which carries its own RGB in the packet. See
Round 12 in `docs/GRENADE_PISTOL.md`; a mixed textured/untextured TMD is safe
because `ProcessTmdTextures` only edits packets with the textured bit set and
advances by each packet's own length byte.

## Local frame and placement

Same convention as the item models: **+Y is the muzzle direction, +X is up, Z is
side to side**, and the fist sits around the origin.

Getting the gun to sit *in the hand* needs two different anchors, and both come
from the original mesh — do not try to derive them from a screenshot (that was
tried; the pixel calibration was ~110 units out):

- **Height and sideways**: put the new barrel's axis where the original weapon
  component's box was (its X centre, 87 for Jill's Beretta). That is the height
  the animation was built around.
- **Along the barrel**: put the new grip's centroid at the **fist's centroid**
  (Y 147). Anchoring by bounding box instead leaves the fingers closed around
  the barrel.

Then one taste knob on top:

- **`HAND_LIFT`** raises the whole gun. A gun with a deeper frame than the
  Beretta's slide box needs it: at lift 0 the flare pistol's trigger sat at
  X -37..-97, below the fist (X -62..48), with nothing under the fingers.
  Useful reference points measured for this model: **45** puts the trigger dead
  centre of the finger band (X 11..-56), **70** brings the top of the trigger
  level with the top of the fist, **90** is what shipped — slightly above a
  strictly correct grip, which reads better on screen.

Also scale from the original weapon component rather than the whole mesh, and
shorten an over-long grip: a grip much taller than the fist leaves the butt
poking out well below her hand.

## Wiring a new weapon id

`g_weaponPathTable[character][weapon_id]` only has rows 0..0xd, so an item id
like `ITEM_GRENADE_PISTOL` (0x71) cannot be a table entry — it needs its own
path constant and a branch in `LoadEquippedWeaponAnimation`.

The subtlety is that the function is reached by two routes carrying different
ids. `SetupCharacterData` passes the raw inventory id, so 0x71 arrives intact.
`menu_update_equipped_weapon` deliberately aliases `equippedWeaponId` to
`ITEM_BERETTA` where it is derived — that alias is what makes the aim/fire state
machine work, so it must not be removed — and on that route the id is already 2.

Recover the real id from the equipped inventory slot instead of caching a flag
(`equippedWeaponId` is assigned from eight places and a flag would drift):

```c
int isGrenadePistol = (weapon_id == ITEM_GRENADE_PISTOL);
if (!isGrenadePistol && weapon_id == ITEM_BERETTA && g_EquippedItemId != 0) {
    isGrenadePistol =
        ((unsigned char*)g_ItemSlotsPointer)[(g_EquippedItemId - 1) * 2] == ITEM_GRENADE_PISTOL;
}
if (isGrenadePistol) weapon_id = ITEM_BERETTA;   // keep Beretta animations
```

This is the same trick `PlayerAnimations.cpp` already uses to recover the id for
the damage type.

Weapon files are per character (`w0x` Chris, `w1x` Jill, …), so a Jill-only item
only needs a Jill file. **Watch the name**: `players/w20.emw` is a real game
file, so the second custom pistol uses `w2f.emw`. Deploy to all three locations
like any other asset (`docs/ASSETS.md`).

## Validation checklist

Assert before shipping: the animation half is byte-identical to the source file;
total size ≤ 37888 with margin; the trailer still reads `{anim_off, tmd_off}`;
`prim_top == 28`, `vert_top == prim_top + n_prim*28`,
`normal_top == vert_top + n_vert*8`, and the TMD ends exactly 8 bytes before EOF;
every packet header is `(9, 6, 0, 52)` (or `0x30000406` for untextured);
every `u` ≤ 127; all vertex and normal indices in range; and **the expected
number of original fist triangles is still present** (count packets whose UV is
not one of yours — 34 for Jill).
