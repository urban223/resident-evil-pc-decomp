# Custom pistols (Jill exclusive) — implementation notes

## What was added
A custom weapon `ITEM_GRENADE_PISTOL = 0x71` (Types.h), given to Jill at
game start via `initial_items[]` in `GameStart.cpp::SetInitialItems()`
(qty 4). Fires using Beretta's animation and Beretta's semi-auto fire
state machine, but deals grenade-launcher damage. It has a hand-authored
orange flare-pistol inventory icon, its own name ("FLARE PISTOL") and
examine description, a dedicated item-examine 3D model built from scratch
(Round 7), its own in-hand model in Jill's hands (Round 8), handgun-style
aiming (Round 9), no ejected shell case (Round 10), and a small hit that
sets the target on fire and burns it down (Round 11).

Round 12 added a **second** weapon, `ITEM_ACID_PISTOL = 0x72` — same mesh,
light-green texture, an acid status effect that makes zombies retch — and
Round 13 a **third**, `ITEM_FREEZE_PISTOL = 0x73`, light blue, which holds
an enemy still until another weapon shatters it. Jill starts with all three.
From Round 12 on, most of the engine's special cases are written against
`ITEM_IS_CUSTOM_PISTOL(id)` rather than one id.

Files touched: `Types.h`, `Globals.h`, `Globals.cpp`, `GameStart.cpp`,
`EntityModelLoader.cpp`, `MainMenu.cpp`, `Rendering.cpp`,
`PlayerAnimations.cpp`, `WeaponDamage.cpp`, `MenuData.cpp`,
`JpnTextTables.cpp`, `RoomInit.cpp`, `entities/{Zombie,EntityCommon}.cpp`.

Binary assets added: `Item_m2/{IFLR,IACD,IFRZ}.ivm` (examine models) and
`players/{w1f,w2f,w3f}.emw` (in-hand models). The `.ivm` are entirely ours —
own mesh, own 256x256 texture — and are tracked in `portdata/`. The `.emw` are
copies of Jill's `W12.EMW` with the weapon TMD swapped, so they carry her
animation data verbatim and are **not** tracked; only our half of them is
(`tools/inhand/*.tmd`), and `tools/build_inhand_pistol.py` grafts it back onto
the `W12.EMW` a clone supplies. See `docs/ASSETS.md`.

Two companion references carry the reusable file-format knowledge:
`docs/IVM_MODEL_FORMAT.md` and `docs/EMW_INHAND_WEAPON_FORMAT.md`.

## Round 1 — display bugs (blank icon / "CRANK" name / "00" qty)
Root causes: `g_ItemImageLookupTable`'s clean per-item format only covers
ids up to ~0x4C; the inventory-grid icon draw has a *separate* hardcoded
path for `itemId >= 0x6f` that samples a static bonus-weapon icon strip
via `texV = itemId*0x1e-2` (real data only for Ingram 0x6f/Minimi 0x70 —
our id read one row past it); `message_item_name_lookup()` (defined in
`Rendering.cpp`, not MainMenu.cpp) reads `g_ItemNamePointers[itemId-1]`,
and slot 112 holds unrelated leftover ROM data ("CRANK"). `display_item_qty`
needed no fix — id > `ITEM_NON_INFINITE_MAX` already draws the ∞ symbol.

## Round 2 — crash on examine + combat not working
**Crash**: access violation at `0x00000008` in `FUN_004841f0` —
`g_itemModelTmdBase` was NULL because the examine model loader has no branch
for our id and fell into an empty filename.

**Combat not working**: the aim/fire/raise state machine in
`PlayerAnimations.cpp` branches on `equippedWeaponId > 0x6e` into an
Ingram/Minimi full-auto path our Beretta-model item cannot play. Fix: alias
`g_playerEntity.equippedWeaponId` itself to `ITEM_BERETTA` at its two source
points — `menu_update_equipped_weapon()` and `SetupCharacterData()` — so every
downstream animation/effect/sound check treats it as an ordinary Beretta.

**This alias matters later**: downstream code cannot tell the item from a real
Beretta. Rounds 8, 9 and 10 all had to work around it by reading the raw item
id out of the equipped inventory slot instead.

## Round 3/4 — inventory icon, name, description
**Icon pixel format**: item icons are **40x30, 8bpp palette-indexed** (the
width passed to `LoadImage` is nominal — the code doubles it). The palette is
always **STATUS.TIM's CLUT index 2**, a 256-entry BGR555 palette shared by
every item icon.

**The background bug**: `idx==0 → alpha 0` in `LoadImage` is real code, but no
actual game asset relies on it. Beretta's own icon contains **zero**
occurrences of index 0 — its "transparent-looking" background is painted with
an **opaque**, panel-colour-matched index (0xE4, near-black navy). Our first
icon's index-0 background rendered as a solid black box. Fix: paint the
background 0xE4 like the game does.

**Item name**: `MenuData.cpp` defines `s_itemFlarePistol = STR("FLARE
PISTOL\x07")` and exposes it as `g_GrenadePistolNamePtr`;
`message_item_name_lookup()` returns it directly.

**Examine description**: `g_ItemDescriptions` was extended by one entry per
weapon, and `g_ItemDescriptionsJpn` in lockstep — they must stay the SAME
length, because `set_item_description_message` bounds-checks the index against
the USA table and then indexes whichever one `GetAssetVersion()` picks.

**Cyrillic limitation**: RE1's encoder (`PrintText.h`'s `encodeChar`) only maps
ASCII; there is no Cyrillic font asset in this game at all. Descriptions are
therefore English.

## Round 5 — the three-tree deploy rule
The first dedicated `.ivm` crashed exactly like Round 2. Root cause:
`assets/USA/...` is **not** where the running exe reads game data from — the
built exe reads `bin/Debug/USA/...` and `bin/Release/USA/...`. `LoadFile` on
the missing path left `g_itemModelTmdBase` NULL.

**Standing rule: every new binary asset goes in all three trees.** See
`docs/ASSETS.md`, which is now the one place that says so.

## Round 6 — the limits of moving vertices
Reshaping Beretta's mesh harder was tried and rejected:
- Laplacian smoothing of the barrel — produced an asymmetric "hook" flap.
- A cylindrical radius-profile remap — Beretta's mesh is not topologically a
  tube, so forcing a circular cross-section onto flat panel groups produced
  warped, self-intersecting wedges.
- Inflating the trigger guard into a ring — the guard is two thin crossing
  struts, not a ring of vertices, so scaling that cluster produced shards.

Conclusion: a real round guard or duckbill grip needs **new geometry**.

Also learned here: judge this mesh with a plain 2D plot of **Z horizontal, -Y
vertical**. A matplotlib 3D `Poly3DCollection` view was misread repeatedly (its
screen axes do not map to data X/Y/Z the way `elev`/`azim` suggest) and cost a
long detour chasing a phantom "missing grip" that was a visualisation artifact.

## Round 7 — examine model rebuilt from scratch
This is the round that produced `docs/IVM_MODEL_FORMAT.md`. The key unlock was
decoding the GT3 primitive packet well enough to *author* one.

A completely new mesh with proportions measured off a reference photo: fat
round barrel with a recessed bore, thin slab frame, large round trigger guard,
raked grip, forward-leaning hammer, lanyard ring. Its own 256x256 8bpp texture.
Every face emitted with an explicit outward hint and auto-oriented, so winding
and normals are consistent by construction.

- **Muted palette** — twice; the viewer's lighting brightens the stored albedo
  a lot, so the final orange is 160,72,28.
- **Outlines.** The classic inverted-hull shell is *invisible against the
  examine screen's black background*. What works is dark lines lying **on** the
  surface along hard edges (faces meeting beyond 35°, chosen because the
  12-sided barrel's own joins are 30°).
- **"FLARE" lettering** required real UV mapping — see the two traps in
  `docs/IVM_MODEL_FORMAT.md`.

**Crash: memory corruption, diagnosed late.** Contours and lettering pushed the
file to 220 KB against a 187160-byte fixed buffer. The 33 KB overrun scribbled
over neighbouring globals and the game crashed elsewhere entirely. Fix without
losing detail: deduplicate vertices and normals after rounding to int16 —
6096 → 3505 vertices, 6096 → **181** normals, 220552 → 152504 bytes.

## Round 8 — in-hand model
Full notes in `docs/EMW_INHAND_WEAPON_FORMAT.md`. The essentials:

**The mesh is the fist and the weapon together.** Replacing the whole TMD
deleted Jill's hand — fine while aiming, because the weapon covered the gap,
and visibly missing while walking.

Two wrong turns worth recording:
1. Concluding the hands came from the character model, because `w10.emw`
   (knife) and `w12.emw` embed a **byte-identical** TMD. They do — but for
   weapon id 0 the engine takes a different branch and never uses the embedded
   copy, so it is just filler.
2. Separating hand from weapon by **texture colour**. Jill's gloves are black,
   so that found only the 8 exposed-skin triangles out of 34. Splitting by
   **connected component** is exact.

**No texture of its own** — the model samples the *character's* page, which is
242/256 tiles full, so the new gun borrows two verified-flat blocks from Jill's
own texture.

**Placement** is anchored to the original mesh, not to a screenshot (that was
tried and was ~110 units out).

## Round 9 — auto-aim hit enemies behind the player
**Root cause**: Round 2's damage substitution passed `ITEM_BAZOOKA_EXPLOSIVE`
straight into `apply_weapon_damage`. That argument does far more than pick a
damage number — `weaponAdj = weapon_id - 1` selects *all* of: the range row,
the hit-detection callback, the line-of-sight gate (which only runs for
`weaponAdj < 5`), the hit-record index and the post-hit callback. So the item
inherited the grenade launcher's *targeting*: a 360° radial test around
`g_playerPosScratch` — a scratch position the launcher fills with its own flying
projectile, which this item never spawns — and no LOS check.

**Fix**: split "what the shot does" from "how the shot is aimed". A new global
`g_weaponDamageIdOverride` is applied inside `apply_weapon_damage` *after* the
target-found and LOS guard:

```c
if (enemy == NULL || (check_weapon_line_of_sight(...) && weaponAdj < 5)) { return 0; }
if (g_weaponDamageIdOverride != 0) {
    weapon_id = g_weaponDamageIdOverride;
    weaponAdj = (unsigned char)(weapon_id - 1);
}
```

Everything above the guard uses the weapon the shot was aimed with; everything
below uses the weapon the shot represents.

## Round 10 — no ejected shell casing
The first fix was **wrong**, and the record of why is the useful part.

The obvious suspect was joint 15: the player skeleton has a **sixteenth joint**
that no animation drives, carrying its own TMD, with its spare shorts as a
state machine, armed by `fire_reset_joint15_recoil`. Gating that changed
nothing — the case still flew.

**Where the case actually lives**: the table `g_weaponMuzzleFlash`, which this
decomp had named "big muzzle flash". It is the ejected case. Three independent
tells, any one of which settles it:

- Both revolvers have `b0 = 99` — never spawns. A python has a muzzle flash; it
  has no case to throw.
- The shotgun's entry fires at frame **25**, when the pump is racked.
- Its spawn is the only one anywhere that tags the effect with
  `animHeader[3] = weaponIdx`, and that tag is exactly what
  `effect_behavior_gravity_impact` tests before playing the landing sound.

`g_weaponFireBillboard` (type 17) is the flash proper and carries the ammo
decrement; `g_weaponFlash2` (type 9) is the smoke puff. Neither was touched.
The `Globals.cpp` comment on the table was corrected in the same pass.

## Round 11 — the flare sets enemies on fire
**RE1 has no damage-over-time of any kind.** The flamethrower and the GL's flame
rounds look like a burn system but are not: their hit records carry a large lump
of damage plus a fire billboard, and nothing on the enemy remembers it was ever
alight.

**What the shot does now**: the damage override is set to `ITEM_BERETTA`, so the
hit is an ordinary handgun hit, and `g_weaponStatusEffect` is read inside
`apply_weapon_damage` right after the post-hit callback. It has to be read
there: that is the only place that knows *which* enemy was hit.

**The burn** (`weapon_update_status_effects`, ticked once per frame from the end
of `update_player_anim`): 4 damage every 15 frames for 16 ticks.

Two design points worth keeping:
- **State lives in WeaponDamage.cpp, not on `Entity`.** The struct is ROM-shaped
  and has no spare byte. Burns are keyed by the enemy's slot in `g_EnemiesList`,
  and because a slot is reused, each burn also records the entity's
  `death_event_id` and stops as soon as the occupant stops matching.
- **`hit_state` is not written while the enemy lives.** Writing it every tick
  would re-trigger the damage reaction and hold the enemy in a permanent flinch.
  Only the death handoff writes it, crediting `ITEM_BAZOOKA_FLAME`.

## Round 12 — a second weapon: ACID PISTOL
The useful record is the *inventory* of what a second weapon costs, and the
three things that did not generalise for free.

**One id test became a macro.** `ITEM_IS_CUSTOM_PISTOL(id)` covers the sites
that only need to know the item is one of ours (12 of them); the sites that
genuinely need to tell them apart keep an explicit id test.

**Tables extended by one.** The two description tables, and
`g_weaponFireData` / `g_weaponFireBillboard` / `g_weaponMuzzleFlash` /
`g_weaponFlash2`. Those last four rows are inert — they exist so the arithmetic
cannot run off the end.

**Three things that did not generalise.**

1. **`players/w20.emw` is a real game file.** The obvious name for item 0x72's
   in-hand model would have quietly overwritten it. Use `w2f.emw`.

2. **Jill's texture page has no green — so the in-hand gun is not textured at
   all.** The whole page was scanned against *both* CLUTs: the only green
   anywhere is the near-black the flare gun already uses.

   The way out is that this engine renders untextured TMD polygons. Primitive
   kind **`0x30000406`** carries its own RGB in the packet. Two details make a
   mixed mesh safe: `ProcessTmdTextures` only edits packets with the textured
   bit set, and its cursor advances by each packet's own length byte. And
   `Store` scales the packet's colour bytes by **1/1024**, not 1/255, so 255 is
   as bright as an untextured surface gets.

3. **CLUT 2's greens are sparse.** The nearest-colour search kept snapping the
   green highlight onto a near-white entry; the four body tones are pinned to an
   explicit ramp picked by hand.

**The burn became a status system**, driven by a small `StatusDef` table:

```
              tick   ticks  dmg   cries        borrowed record
FIRE          15f    16     4     4 / 9        weaponAdj 8 (GL flame)
ACID          20f    20     3     7 / 9        weaponAdj 7 (GL acid)
```

`record` is the whole trick: each status borrows an existing weapon's hit
record, so its billboard, its per-enemy height offset and its death state are
the ones the engine already ships.

**The vomit — and three wrong turns before it.** The zombie already knows how to
be sick, and it is an *attack*: `zombie_vomiting`, `zombie_action_tbl[6]`.

1. **Copying the billboard verbatim** — type 0 is the shared **gore** sheet:
   that is a blood splat.
2. **Re-tinting it.** It was still a blood *splat*, just a green one. The real
   spew is type 0x20, a **room** sprite, which draws in its own authored colours
   with no tint applied at all.
3. **Writing `action_behavior = 6` and letting the state machine dispatch it.**
   Correct in principle, unreliable in practice: `action_behavior` is only
   dispatched at the very END of `zombie_chase_player`, and that function bails
   into `zombie_attack` first whenever the player is inside its cone — which is
   exactly when the player is watching.

What works is a small exported trigger that arms the behaviour AND runs its
first sub-state immediately, so the spew and the sound land even if the chase
logic steals the zombie back on the next frame.

The general lesson, learned three times over: **when the game already performs
the thing, drive its behaviour and let it spawn its own effects**, rather than
reconstructing the effect from outside.

## Round 13 — a third weapon: FREEZE PISTOL
The wiring was mechanical after Round 12. One extra table needed extending that
Round 12 did not: **`g_weaponFireEndFrame`** — 0x73 - 99 = 16, one past its end.

**Freezing anything, not just zombies.** There is no per-entity "asleep" bit to
borrow: `status_flags` bit 0 also gates the draw loop, so clearing it makes an
enemy *invisible*, not still. What is portable is the dispatch itself, in
`update_entities`:

```c
if ((ENTITY->status_flags & 0x01) != 0) {
    if (!weapon_status_entity_frozen(ENTITY)) {
        void* updateFunc = enemies_update_functions_tbl[ENTITY->id];
        if (updateFunc != NULL) ((void(*)())updateFunc)();
    }
```

Skipping that call is a complete freeze, because an enemy only translates
through `Add_speedXZ` and only advances an animation frame through `Joint_move`,
both reached exclusively from inside a state handler. Everything that keeps the
enemy present sits outside it.

**The blue does not come from a billboard.** An effect's colour resolves through
the room's own blend slot, and **the smoke sheet has no blue row anywhere in the
game** — tint 1 is literally grey in half the rooms. So the **enemy itself** is
tinted icy blue with `JointApplyColorTint` over every joint, put back to neutral
when the freeze ends, when it dies, or when it shatters.

Two lessons: **a billboard's sheet decides how it looks, its animation record
decides where it goes** — check whether an effect moves before using it as a
status marker; and **the blend slot is per-room**, so "what colour is effect
type N" has no single answer.

**A frozen enemy has to be kept shootable, and by default it is not.** Two
conditions in the target loop are the reason, and both follow from the enemy's
update being skipped: `hit_state` is never cleared, and the upper `status_flags`
bits are never recomputed. So each freeze tick clears `hit_state` and forces all
three aim bits.

**There is no safe way to make an enemy simply vanish**: clearing `status_flags`
bit 0 alone leaves the draw loop walking past the end of `g_EnemiesList`, and
decrementing `g_enemy_count` is worse, because that field doubles as the spawn
slot index for `cmd_enemy_set`. So instead the body **comes apart**, using the
engine's own severed-limb mechanism (`joint->flags |= 0x0C`, next joint `|=
0x10`) applied to every joint but the root.

**A bug the third weapon exposed: the in-hand model never reloaded.** With one
custom pistol, equipping it always changed `equippedWeaponId`. With three,
swapping one for another leaves it at `ITEM_BERETTA` — the change test sees
nothing and whatever `.emw` was loaded last stays in the buffer. The loader now
records what it loaded in `g_loadedCustomPistol` and the menu compares the newly
equipped real item id against that.

## Round 14 — making the shatter look like ice
Three separate causes, and the first two are the generally useful ones.

**Effect type 8 spawns, and draws nothing.** The obvious sheet for ice is the
GLASS sheet. Nothing appeared — and every check said it should have. The one
thing the shipped game ever does with it is `effect_behavior_charge`'s own
`(8, 1)`: a sprite authored to be *driven* by that behaviour. The practical
rule: before using an unfamiliar effect type, find where the game spawns it — if
there is exactly one call site and it is inside a behaviour that manages the
effect's own phases, that type is not a general-purpose billboard.

**The anchor pointer is live, not a snapshot.** `Effect_CreateBillboard` stores
`eff->spriteInfo` and the renderer dereferences it every frame. Anchoring a
shard to a joint that was severed one line earlier hands it a matrix that
`FUN_004896c0` immediately takes over as scratch.

**Every severed piece flies the identical arc.** `render_entity` rewrites the
launch velocity from scratch on every frame before the ballistic step — the same
vector for every piece. Two fields *do* survive that rewrite: **`field_02`**,
the frame counter gravity is multiplied by (seeding it per piece gives each one
its own apex), and **`world.t`**, which the step only ever *adds* to. Fanning
both across eight X/Z pairs and eight apex values spreads the pieces into a
burst.

The pieces keep the ice tint instead of being reset to neutral: lit blue chunks
read as shards, flesh-coloured ones read as dismemberment. That single line was
doing more damage to the effect than the missing sprites were.

## Status
Rounds 1-14 are confirmed working in-game.

Known, accepted gaps:
- The **item-box icon draw path** still indexes `g_ItemImageLookupTable` by raw
  id with no alias. Never tested. Affects all three weapons.
- The FLARE pistol's in-hand model is **terracotta, not the icon's bright
  orange**, because it samples Jill's already-full texture page. The acid pistol
  solved this by going untextured; the flare gun could be converted the same way.
- The in-hand gun's grip butt sits inside the fist rather than protruding below
  it, a side effect of the chosen `HAND_LIFT`.
- Both pistols still play the **Beretta's fire report** and the handgun muzzle
  flash; only the shell case was removed.
- Effect type **8 (the glass sheet) is unusable as a loose billboard**. If
  ice-specific art is ever wanted, it needs a sprite added to the effspr pages.
