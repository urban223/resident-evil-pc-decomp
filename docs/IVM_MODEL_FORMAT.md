# Authoring `.ivm` item models from scratch — format reference

Everything here was confirmed by decoding `assets/USA/Item_m2/I00V.IVM`
(Beretta) byte-by-byte and by reading the engine's own parser
(`src/marni/Marni3DObject.cpp`, `src/game/MainMenu.cpp`). It is enough to
generate a brand-new textured model file that this engine loads — no Beretta
geometry or texture required. Written while building the flare pistol's
from-scratch model (Rounds 7-8 of `docs/GRENADE_PISTOL.md`).

## THE FILE SIZE LIMIT — read this first

`menu_load_item_model` loads the whole `.ivm` into a **fixed global buffer**:

```c
LoadFile(DAT_008e1cb0, g_TimImageBuffer__bitmap, 0x20);
// g_TimImageBuffer__bitmap == g_TimImageBuffer + 20
// extern BYTE g_TimImageBuffer[187160+20];
```

So an item model may be at most **187160 bytes**. The `0x20` third argument is
*flags*, not a size — `LoadFile` does a plain `fread` of the entire file into
the buffer with **no bounds check whatsoever**. Overrun it and the read silently
scribbles over the globals that follow.

This failure does not look like a file problem. A 220KB build of the flare
pistol loaded and displayed fine, then crashed later and elsewhere — a read
access violation in `TmdAnimation.cpp::FindMinClutDepth`, walking an animation
slot whose `data2` pointer had been overwritten with `0xF9980000`. Classic
memory corruption: it falls over far from where it broke. If a model change
produces a crash in unrelated code, check the file size before anything else.

Budget: the TIM half is fixed at 66080 bytes (8 + 524 + 65548), leaving
**121080 bytes for the TMD**. Emitting three unique vertices and three unique
normals per triangle costs 76 bytes/triangle, which caps you at ~1590
triangles — deduplicate instead (see below) and the same triangle count costs
roughly half that.

Put a hard assert in the generator, not a mental note.

## Deduplicate vertices and normals

A generator that emits fresh entries per triangle wastes most of the file. Merge
them after rounding to int16 — on the flare pistol this cut 6096 vertices to
3505 and 6096 normals to **181**, taking the file from 220552 to 152504 bytes
with no visual change at all.

Sharing is safe in TMD specifically:

- UVs live in the **primitive**, not the vertex, so merging identical positions
  can never merge texture coordinates — the usual reason a mesh needs split
  vertices does not apply here.
- Flat shading survives, because each triangle still references its own normal
  index; two coplanar faces sharing one identical normal direction shade
  identically anyway. Hence normals collapse hardest — every face of a flat
  panel points the same way.

## File = TIM texture + TMD mesh, concatenated

```
struct { u32 id = 0x10; u32 flags; }            // TIM header
u32 clut_len; u16 clut_x, clut_y, clut_w, clut_h; u8 clut_data[clut_len-12]
u32 img_len;  u16 img_x,  img_y,  img_w,  img_h;  u8 img_data[img_len-12]
... TMD begins immediately after ...
```

`MainMenu.cpp::FUN_00484420` validates `id == 0x10 && (flags & 7) <= 2` and
computes the TMD base as `p + 8 + clut_len + img_len`. Anything else leaves
`g_itemModelTmdBase` NULL and the examine screen crashes with the familiar
access violation at `0x00000008`.

Beretta's values, which a new model should simply reuse:

| field | value | meaning |
|---|---|---|
| `flags` | `0x9` | bit0-1 = 1 → **8bpp indexed**; bit3 → CLUT present |
| clut | x=0, y=480, w=256, h=1 | 256-entry BGR555 palette at VRAM (0,480) |
| img | x=0, y=0, w=128, h=256 | **w is in 16-bit words**: 128 words = 256 texels wide, so the image is 256x256 8bpp = 65536 bytes |

BGR555 packing: `(b>>3)<<10 | (g>>3)<<5 | (r>>3)`.

## TMD layout

```
u32 id = 0x41; u32 flags = 0; u32 nobj = 1
s32 obj[7] = { vert_top, n_vert, normal_top, n_normal, prim_top, n_prim, scale }
<primitives> <vertices> <normals>
```

The three `*_top` fields are offsets **relative to the start of the object
table** (i.e. `tmd_base + 12`), not to the file or the TMD header. The parser
computes `base = ((offset + 0xC) & ~3) + tmd_header`, so every offset must keep
`offset + 12` 4-byte aligned. Beretta's block order is primitives → vertices →
normals with `prim_top = 28` (immediately after the 28-byte object table);
following that order makes the offsets trivial to compute, and
`normal_top + n_normal*8 == file size` exactly.

**`ResolveAnimPointers` rewrites those offsets in place.** It walks the object
table, replaces each entry's vertex / normal / primitive offset with
`objectTable + offset`, and sets FIXP — bit 0 of the flags word at
`tmd_base + 4`. So `table + entry[0]` reads vertices before that call and
base-plus-a-heap-address after it. Anything that reads the table itself must
either run first or test the flag (`raid_model_extent` in
`src/game/RaidItemModels.cpp` does the latter).

Vertices and normals are both 8-byte `SVECTOR`s: `s16 x, y, z, pad`. Normals
are unit vectors scaled by **4096** (PS1 4.12 fixed point) — the parser
multiplies by `0.00024414063` (= 1/4096).

The same 7-int32 object-table entry is what `Entities.h`'s `AnimSlot` describes
(`data0`/`pad_04`/`data1`/`pad_0c`/`data2`/`entryCount`/`pad_18`), which is why
a corrupted model shows up as a bad `AnimSlot`.

## Coordinate system

- **Z**: muzzle negative → grip/rear positive
- **Y**: up negative → down positive (the grip hangs toward +Y)
- **X**: left/right, symmetric about 0

`PSXObjReadVertex` negates Y when loading (PS1 +Y down vs D3D +Y up), so a model
authored with +Y = down displays upright. Beretta spans roughly
x ±350, y ±1500, z -2510..2450 — matching that overall size keeps a new model
the right scale in the examine viewer.

## Primitive packets

Every Beretta primitive is the same kind, and it is the one to use:

```
u8 olen=9, ilen=6, flag=0, mode=52(0x34)     // header dword = 0x34000609
u16 body[12] = { u0v0, cba, u1v1, tsb, u2v2, pad,
                 norm0, vert0, norm1, vert1, norm2, vert2 }
```

Packet size = `4 + ilen*4` = 28 bytes.

`0x34000609` is the **textured Gouraud triangle** case in
`PSXObject_Store`. The critical detail — and the one that is easy to get wrong —
is that vertex and normal indices are **interleaved per corner**, with the
vertex index in the HIGH half of each dword and the normal index in the LOW
half. Assuming a grouped `[n0,n1,n2,v0,v1,v2]` layout produces mostly
out-of-range indices; the interleaved reading gives zero.

UVs are bytes: `u` in the low byte, `v` in the high byte of each `uNvN` word.
The parser divides `u` by `texRef` and `v` by 256; `MainMenu.cpp` passes
`texRef = 0x100`, so both axes map 0-255 onto the 256x256 texture directly.

`cba` (CLUT attr) `= 30720 (0x7800)` → x = `cba & 0x3F` = 0, y = `cba >> 6` =
480, matching the TIM's CLUT position. `tsb` (texpage attr) `= 128 (0x80)` →
page (0,0), colour mode 1 = 8bpp. Keep both constant across all primitives:
that makes the model a single "kind", which sidesteps the parser's 16-kind
limit and its `clut range` rejection when kinds disagree by more than 1.

`mode` bit1 (`0x02`, ABE) must stay clear unless the model really is
semi-transparent — `CheckTmdTransparency` routes any model with it set through
a second "STP page" texture upload and 50% blending.

## Winding and normals

Beretta's stored normal is consistently ≈ `-cross(p1-p0, p2-p0)` (dot ≈ -0.8
against it on every triangle sampled). So vertices are ordered **clockwise seen
from outside**, and the outward normal equals `cross(p2-p0, p1-p0)`.

The robust way to author this without reasoning about winding per face: build
each face with an explicit approximate outward direction, then auto-orient —
swap `p1`/`p2` whenever `dot(cross(p1-p0,p2-p0), outward) > 0`, and store
`outward` as the normal. Lighting only uses the normal; culling uses the
winding; doing both from one hint keeps them consistent. If a face carries
per-corner UVs, swap those with their corners.

Backface culling is real and is done in screen space:
`TmdRenderer.cpp` keeps a primitive only when
`(x1-x0)*(y2-y0) - (x2-x0)*(y1-y0) > 0` over its first three screen vertices.
That is what makes the outline trick below work.

## UV mapping vs flat colours

**Flat colours (no unwrapping).** Fill part of the texture with large
single-colour swatches and give all three corners of a triangle the **same** UV
at a swatch's centre. The face samples one constant colour and Gouraud lighting
still shades it. The parser nudges `v1.u`/`v1.v` by `0.0001` when all UVs match
(avoiding a degenerate gradient) — 0.026 texels, harmless inside a large swatch.

**Real mapping** is needed for anything with detail — lettering, scratches,
panel lines — because a flat-colour face can only ever be one colour. Map the
quad's four corners onto a texture rectangle. Two traps, both hit on the flare
pistol's barrel:

- **Which quad edge U follows.** A cylinder built as
  `(ring[k], ring[k+1], back[k+1], back[k])` runs *around* the circumference
  first and *along* the tube second, so the naive corner order squashes
  lettering across one facet and smears it down the barrel. Transpose the UV
  corners so U follows the axis you actually want.
- **The opposite side is rotated, not mirrored.** On the facet facing −X both
  the along-axis and around-axis directions reverse, so text needs a **180°
  turn (flip U *and* V)**. Flipping U alone leaves it upside down — which at a
  glance reads as "mirrored" and sends you after the wrong bug.

Verifying either of these needs a preview that actually samples the texture
through the UVs; a preview that paints each face one colour cannot show
lettering at all and will happily hide the mistake.

## Outlines on a black background

The examine viewer's background is black, so the classic inverted-hull
silhouette outline in black is **invisible** — it only shows where it falls
across the model itself. What reads instead are dark lines lying *on* the
surface along hard edges: find edges whose two faces meet beyond an angle
threshold and lay a narrow dark quad along each, lifted slightly off the
surface. Pick the threshold against the model's own facets — a 12-sided
cylinder has 30° joins, so a threshold below that stripes the barrel like a
barrel.

The inverted hull is still worth keeping for seams where two parts
interpenetrate and share no edge. Offset each shell face along **its own**
normal and grow it in-plane; offsetting along averaged vertex normals dips the
shell under the surface on hard edges and streaks across facets.

## Hard limits to respect

- **File size ≤ 187160 bytes** — see the top of this document. This is the one
  that bites hardest, because it fails silently and far away.
- `nobj` must satisfy `1 <= nobj < 3` (`FUN_004841f0` returns early otherwise).
- Distinct primitive kinds must be `< 17`. One kind is ideal.
- The renderer element's vertex buffer is sized `n_prim * 3` (**not** `n_vert`).
  `PSXObject_Resize` halves any element reaching **1024** vertices, and bails
  past 16 elements — so about 5400 triangles. Beretta is 496 prims → 1488
  element vertices, already two elements; the flare pistol at 2032 prims is
  eight. The file-size cap binds long before this does.
- Vertex/normal indices are u16, so both counts must stay under 65536.
- `TmdRenderer.cpp` caps the per-frame pool at `TMD_MAX_TRIS_COLLECT` 8192
  triangles and `TMD_MAX_QUEUE` 2048 objects — not a constraint at item scale.

## Validation checklist before deploying

Re-parse the generated file and assert: **total size ≤ 187160** (with margin);
TIM id/flag; `tmd id == 0x41`, `flags == 0`, `nobj == 1`; `prim_top == 28`,
`vert_top == prim_top + n_prim*28`, `normal_top == vert_top + n_vert*8`;
`obj_table_off + normal_top + n_normal*8 == file size`; every
`(offset + 12) % 4 == 0`; one distinct kind; ABE clear on every packet;
`max vertex index < n_vert` and `max normal index < n_normal`; UVs within
0-255; and simulate the element split to confirm it stays under 16.

Deployment is the standing three-location rule from `docs/ASSETS.md`:
`assets/USA/Item_m2/`, `bin/Debug/USA/Item_m2/` and
`bin/Release/USA/Item_m2/` — the exe reads the `bin/<Config>` copies, and a
missing file there crashes exactly like a malformed one.
