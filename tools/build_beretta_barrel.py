#!/usr/bin/env python3
"""
CUSTOM: gives Jill's in-hand Beretta a BARREL, so the slide going back has
something to uncover.

The stock players/W12.EMW models the pistol as a single eight-vertex box - the
slide, and nothing else, because the grip is hidden inside the fist and the
barrel is hidden inside the slide. That is fine while the slide never moves.
Once it does, it is the whole problem: a featureless box sliding backwards does
not read as a slide going back, it reads as the gun getting shorter. On a real
pistol what tells you the slide is locked is the BARREL standing exposed in
front of it.

So this adds one: a second, thinner box that runs through the slide and pokes a
few units past the muzzle. At rest it is invisible - fully enclosed by the
slide, except that muzzle ring. With the slide back it is a barrel.

It is purely additive. The 34 fist triangles and the 12 slide triangles are
copied byte for byte, the animation half is copied byte for byte, and the
existing vertices and normals keep their indices, so every pose and the whole
resting silhouette are exactly what they were. The file gains 8 vertices and
12 triangles.

The barrel is given the UVs of the slide's MUZZLE FACE rather than its sides.
Those are two different texels on Jill's character page and the face one is
visibly lighter - which is free contrast, and contrast is what was missing.
"""

import os, struct

SRC = "assets/USA/players/W12.EMW"
TREES = ["assets/USA/players", "bin/Debug/USA/players", "bin/Release/USA/players"]

# ---------------------------------------------------------------------------
# The barrel, in the model's frame: +Y is the muzzle direction, +X is up,
# Z is side to side. The slide it hides in is X 47..127, Y 84..424, Z -27..27.
#
# Thinner than the slide on both axes so it is completely swallowed at rest,
# and 8 units longer so the muzzle reads as a ring rather than a flat face.
# ---------------------------------------------------------------------------
# Sits HIGH in the slide, not centred in it. On a real pistol the barrel runs
# along the top of the slide's inside and the recoil spring takes the space
# underneath, so a barrel on the centreline hangs visibly low once the slide is
# back - which is exactly how the first cut looked.
B_XLO, B_XHI = 78, 118
B_ZLO, B_ZHI = -14, 14
B_YLO, B_YHI = 150, 436

# The barrel's body takes the slide's MUZZLE-FACE texel (packets 36/37 of the
# stock file), which is a lighter grey than the texel its sides use - free
# contrast against the slide it slides out of.
B_U, B_V = 3, 95

# Its own muzzle face takes a near-black one instead, so the end of the barrel
# reads as a BORE and not as a grey stub. This is the flat dark block on Jill's
# character page catalogued in claude/emw-inhand-weapon-format.md (u 42..53,
# v 92..143, about rgb(8, 24, 16)); one texel, all three corners on it.
BORE_U, BORE_V = 47, 110

PKT_HDR = 0x34000609          # gouraud textured triangle, the file's only kind


def read_emw(path):
    d = open(path, "rb").read()
    n = (len(d) & ~3) - 8
    anim_off, tmd_off = struct.unpack_from("<2I", d, n)
    return d, anim_off, tmd_off


def parse_tmd(d, tmd_off):
    base = tmd_off + 12
    vo, nv, no, nn, po, npr, scale = struct.unpack_from("<7I", d, base)
    prims, p = [], base + po
    for _ in range(npr):
        h = struct.unpack_from("<I", d, p)[0]
        ilen = (h >> 8) & 0xFF
        prims.append((h, struct.unpack_from("<%dI" % ilen, d, p + 4)))
        p += 4 + ilen * 4
    verts = [struct.unpack_from("<4h", d, base + vo + i * 8) for i in range(nv)]
    norms = [struct.unpack_from("<4h", d, base + no + i * 8) for i in range(nn)]
    return dict(base=base, vo=vo, nv=nv, no=no, nn=nn, po=po, npr=npr,
                scale=scale, prims=prims, verts=verts, norms=norms)


def box_verts(xlo, xhi, ylo, yhi, zlo, zhi):
    """The stock slide's corner ORDER, so the triangle table below transfers."""
    return [
        (xlo, yhi, zhi), (xlo, yhi, zlo), (xlo, ylo, zhi), (xlo, ylo, zlo),
        (xhi, yhi, zhi), (xhi, yhi, zlo), (xhi, ylo, zhi), (xhi, ylo, zlo),
    ]

# Lifted straight off the stock slide's packets 34..45, as local corner indices.
# Copying the winding rather than deriving it is the point: the renderer culls
# by signed area, and a box wound the other way is a box you can see through.
BOX_TRIS = [
    (2, 6, 7), (2, 7, 3), (4, 0, 1), (4, 1, 5),
    (4, 6, 2), (4, 2, 0), (5, 7, 6), (5, 6, 4),
    (1, 3, 7), (1, 7, 5), (0, 2, 3), (0, 3, 1),
]


def build():
    root = os.environ.get("RE_ROOT", ".")
    d, anim_off, tmd_off = read_emw(os.path.join(root, SRC))
    t = parse_tmd(d, tmd_off)

    assert t["npr"] == 46 and t["nv"] == 27, (t["npr"], t["nv"])
    assert all(h == PKT_HDR for h, _ in t["prims"]), "unexpected packet kind"

    # The normals the slide already uses. The barrel is the same shape, so the
    # same per-corner normals shade it the same way and no new normal is needed.
    slide_norms = [ (t["prims"][34 + i][1][3 + k] & 0xFFFF) for i in range(12)
                    for k in range(3) ]

    first_new_vert = t["nv"]                       # 27
    bverts = box_verts(B_XLO, B_XHI, B_YLO, B_YHI, B_ZLO, B_ZHI)

    def uvwords(u, v):
        return ((u & 0xFF) | ((v & 0xFF) << 8) | (30720 << 16),   # cba
                (u & 0xFF) | ((v & 0xFF) << 8) | (128 << 16),     # tsb
                (u & 0xFF) | ((v & 0xFF) << 8))

    body_uv = uvwords(B_U, B_V)
    bore_uv = uvwords(BORE_U, BORE_V)

    new_prims = []
    for i, (a, b, c) in enumerate(BOX_TRIS):
        n0, n1, n2 = slide_norms[i * 3: i * 3 + 3]
        # The two triangles whose every corner is at the muzzle end (local
        # indices 0, 1, 4, 5 are the yhi corners) are the bore.
        is_bore = all(k in (0, 1, 4, 5) for k in (a, b, c))
        uvw0, uvw1, uvw2 = bore_uv if is_bore else body_uv
        body = (uvw0, uvw1, uvw2,
                ((first_new_vert + a) << 16) | n0,
                ((first_new_vert + b) << 16) | n1,
                ((first_new_vert + c) << 16) | n2)
        new_prims.append((PKT_HDR, body))

    prims = t["prims"] + new_prims
    verts = t["verts"] + [(x, y, z, 0) for (x, y, z) in bverts]
    norms = t["norms"]

    # ---- lay the TMD out again --------------------------------------------
    po = 0x1C
    prim_bytes = b"".join(struct.pack("<I", h) + struct.pack("<%dI" % len(b), *b)
                          for h, b in prims)
    vo = po + len(prim_bytes)
    vert_bytes = b"".join(struct.pack("<4h", *v) for v in verts)
    no = vo + len(vert_bytes)
    norm_bytes = b"".join(struct.pack("<4h", *nv) for nv in norms)

    tmd = struct.pack("<3I", 0x41, 0, 1)
    tmd += struct.pack("<7I", vo, len(verts), no, len(norms), po, len(prims), t["scale"])
    tmd += prim_bytes + vert_bytes + norm_bytes

    out = d[:tmd_off] + tmd + struct.pack("<2I", anim_off, tmd_off)

    # ---- the checks from the .emw notes, before anything ships -------------
    assert out[:tmd_off] == d[:tmd_off], "animation half changed"
    assert len(out) <= 37888 - 2048, "too close to g_animationBuffer's 37888"
    n2 = (len(out) & ~3) - 8
    assert struct.unpack_from("<2I", out, n2) == (anim_off, tmd_off)
    r = parse_tmd(out, tmd_off)
    assert r["po"] == 28
    assert r["vo"] == r["po"] + r["npr"] * 28
    assert r["no"] == r["vo"] + r["nv"] * 8
    assert 12 + r["no"] + r["nn"] * 8 == len(tmd)
    assert r["prims"][:46] == t["prims"], "fist or slide packets changed"
    assert r["verts"][:27] == t["verts"], "existing vertices moved"
    for h, b in r["prims"]:
        assert h == PKT_HDR
        for k in range(3):
            assert (b[k] & 0xFF) <= 127, "u out of range"
        for k in range(3):
            assert ((b[3 + k] >> 16) & 0xFFFF) < r["nv"]
            assert (b[3 + k] & 0xFFFF) < r["nn"]
    # the fist must still be all there: 34 packets not using our texel
    ours = ((B_V << 8) | B_U, (BORE_V << 8) | BORE_U)
    kept = sum(1 for h, b in r["prims"] if (b[0] & 0xFFFF) not in ours)
    assert kept == 34 + 12, "expected 34 fist + 12 slide packets to survive, got %d" % kept
    bore = sum(1 for h, b in r["prims"] if (b[0] & 0xFFFF) == ((BORE_V << 8) | BORE_U))
    assert bore == 2, "expected exactly 2 bore triangles, got %d" % bore

    print("W12.EMW: %d -> %d bytes, %d prims, %d verts (barrel Y %d..%d, Z %d..%d)"
          % (len(d), len(out), r["npr"], r["nv"], B_YLO, B_YHI, B_ZLO, B_ZHI))
    return out


def main():
    root = os.environ.get("RE_ROOT", ".")
    blob = build()
    for tdir in TREES:
        p = os.path.join(root, tdir)
        if not os.path.isdir(p):
            continue
        with open(os.path.join(p, "W12.EMW"), "wb") as f:
            f.write(blob)
        print("wrote", p)


if __name__ == "__main__":
    main()
