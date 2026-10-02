#!/usr/bin/env python3
"""
CUSTOM: the low-poly furniture of the RAID bathroom, as .obj files.

The room (raid1.lvl) is modelled after the guardhouse bathroom's two
backgrounds, which are its reference: sizes and places were measured off them
(the room's own camera records, rays cast through the pictures), the shapes are
modelled here by hand, in code, so they can be re-shaped by editing numbers.

Every model is in GAME UNITS with Y NEGATIVE UP, origin on the floor under its
middle, and is placed by a `mesh` line in raid1.lvl. Faces are wound so that
(b - a) x (c - a) points OUT of the solid - RaidArena.cpp culls by that - and
large faces are cut small, because the arena's light is evaluated per vertex.

These are this project's own geometry, not Capcom's, so they are tracked:
written to portdata/USA/Data/raidmesh/ and copied into the asset trees by
tools/deploy_portdata.py.

    python tools/build_raid_meshes.py && python tools/deploy_portdata.py
"""

import math, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(ROOT, "portdata", "USA", "Data", "raidmesh")


class Mesh:
    def __init__(self):
        self.v = []
        self.f = []

    def vert(self, x, y, z):
        self.v.append((x, y, z))
        return len(self.v) - 1

    def tri(self, a, b, c, outward=None):
        """Add a triangle; if `outward` (a point INSIDE the solid) is given,
        wind it so its normal points away from that point."""
        if outward is not None:
            A, B, C = self.v[a], self.v[b], self.v[c]
            u = [B[i] - A[i] for i in range(3)]
            w = [C[i] - A[i] for i in range(3)]
            n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
            ctr = [(A[i] + B[i] + C[i]) / 3 for i in range(3)]
            if sum(n[i] * (ctr[i] - outward[i]) for i in range(3)) < 0:
                b, c = c, b
        self.f.append((a, b, c))

    def quad(self, a, b, c, d, inside):
        self.tri(a, b, c, inside)
        self.tri(a, c, d, inside)

    def save(self, name, double=False):
        """`double`: also write every face reversed, on vertices of its own
        (so the smooth normals of the two sides do not cancel) - for cloth,
        thin enough that a face the culling drops shows what is behind it."""
        if double:
            base = len(self.v)
            self.v += list(self.v)
            self.f += [(a + base, c + base, b + base) for a, b, c in self.f]
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, name), "w", newline="\n") as fh:
            fh.write("# %s - tools/build_raid_meshes.py. Game units, Y negative up.\n" % name)
            for x, y, z in self.v:
                fh.write("v %.1f %.1f %.1f\n" % (x, y, z))
            for a, b, c in self.f:
                fh.write("f %d %d %d\n" % (a + 1, b + 1, c + 1))
        print("%s: %d verts, %d tris" % (name, len(self.v), len(self.f)))


def box(m, x0, y0, z0, x1, y1, z1, cells=1):
    """A box, its faces cut into `cells` x `cells`."""
    inside = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
    def face(o, du, dv):
        g = [[m.vert(o[0] + du[0] * i / cells + dv[0] * j / cells,
                     o[1] + du[1] * i / cells + dv[1] * j / cells,
                     o[2] + du[2] * i / cells + dv[2] * j / cells)
              for j in range(cells + 1)] for i in range(cells + 1)]
        for i in range(cells):
            for j in range(cells):
                m.quad(g[i][j], g[i + 1][j], g[i + 1][j + 1], g[i][j + 1], inside)
    dx, dy, dz = x1 - x0, y1 - y0, z1 - z0
    face((x0, y0, z0), (dx, 0, 0), (0, 0, dz))      # top (y0 is the top: Y up is negative)
    face((x0, y1, z0), (dx, 0, 0), (0, 0, dz))      # bottom
    face((x0, y0, z0), (0, dy, 0), (0, 0, dz))
    face((x1, y0, z0), (0, dy, 0), (0, 0, dz))
    face((x0, y0, z0), (dx, 0, 0), (0, dy, 0))
    face((x0, y0, z1), (dx, 0, 0), (0, dy, 0))


def ring(m, cx, cz, a, b, y, n, power=4.0):
    """A superellipse ring |x/a|^p + |z/b|^p = 1 - a rounded rectangle at p=4,
    an ellipse at p=2."""
    out = []
    for i in range(n):
        t = 2 * math.pi * i / n
        c, s = math.cos(t), math.sin(t)
        x = a * math.copysign(abs(c) ** (2 / power), c)
        z = b * math.copysign(abs(s) ** (2 / power), s)
        out.append(m.vert(cx + x, y, cz + z))
    return out


def band(m, r0, r1, inside):
    n = len(r0)
    for i in range(n):
        j = (i + 1) % n
        m.quad(r0[i], r0[j], r1[j], r1[i], inside)


def cap(m, r, centre, inside):
    c = m.vert(*centre)
    n = len(r)
    for i in range(n):
        m.tri(c, r[i], r[(i + 1) % n], inside)


def vessel(m, cx, cz, a, b, y_base, y_rim, depth, wall, n=32, power=4.0,
           flare=(0.86, 0.97), inner_floor=0.80, lip=0.0, part="all"):
    """A bath / basin / bowl: a tapered outer shell, a rolled lip, a rim and a
    hollow whose wall curves into its floor. Rings share their vertices, so
    the light (smooth normals, RaidArena.cpp) rounds every edge but the ones
    that are meant to be sharp. `part` "outer" is the shell alone, "inner"
    the rim and the hollow - so the two can take different materials."""
    yb, yr = y_base, y_rim
    ym = yr + (yb - yr) * 0.45
    outer = [ring(m, cx, cz, a * flare[0], b * flare[0], yb, n, power),
             ring(m, cx, cz, a * flare[1], b * flare[1], ym, n, power),
             ring(m, cx, cz, a, b, yr + 70, n, power)]
    if lip > 0:
        outer.append(ring(m, cx, cz, a + lip, b + lip, yr + 30, n, power))
        outer.append(ring(m, cx, cz, a + lip * 0.6, b + lip * 0.6, yr, n, power))
    else:
        outer.append(ring(m, cx, cz, a, b, yr, n, power))
    mid = (cx, (yb + yr) / 2, cz)
    if part != "inner":
        for r0, r1 in zip(outer, outer[1:]):
            band(m, r0, r1, mid)
        cap(m, outer[0], (cx, yb, cz), (cx, yb - 10, cz))          # underside, facing down
    if part == "outer":
        return
    # the rim's top, facing up: a slight crown, so it reads as a rolled edge
    ia, ib = a - wall, b - wall
    crown = ring(m, cx, cz, a - wall * 0.45, b - wall * 0.45, yr - 12, n, power)
    i0 = ring(m, cx, cz, ia, ib, yr, n, power)
    up = (cx, yr + 400, cz)
    band(m, outer[-1], crown, up)
    band(m, crown, i0, up)
    # inside: rings down a curve, wound to face the axis
    prof = [0.0, 0.10, 0.28, 0.52, 0.76, 0.92, 1.0]
    inner = [i0]
    for t in prof[1:]:
        k = 1.0 - (1.0 - inner_floor) * (t ** 2.4)
        inner.append(ring(m, cx, cz, ia * k, ib * k, yr + depth * t, n, power))
    for r0, r1 in zip(inner, inner[1:]):
        for i in range(n):
            j = (i + 1) % n
            A = m.v[r0[i]]
            beyond = (cx + (A[0] - cx) * 2.0, A[1], cz + (A[2] - cz) * 2.0)
            m.quad(r0[i], r0[j], r1[j], r1[i], beyond)
    cap(m, inner[-1], (cx, yr + depth, cz), (cx, yr + depth + 10, cz))   # the hollow's floor


def basis(d):
    """Two unit vectors square to the unit vector d."""
    ref = (0.0, 1.0, 0.0) if abs(d[1]) < 0.9 else (1.0, 0.0, 0.0)
    u = (d[1] * ref[2] - d[2] * ref[1], d[2] * ref[0] - d[0] * ref[2], d[0] * ref[1] - d[1] * ref[0])
    L = math.sqrt(sum(c * c for c in u))
    u = tuple(c / L for c in u)
    w = (d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0])
    return u, w


def tube(m, p0, p1, r0, r1=None, n=10, caps=(True, True)):
    """A round tube from p0 to p1, radius r0 tapering to r1, closed at either
    end as asked. Its own vertices (a hard edge where it meets anything)."""
    r1 = r0 if r1 is None else r1
    d = [p1[i] - p0[i] for i in range(3)]
    L = math.sqrt(sum(c * c for c in d))
    d = tuple(c / L for c in d)
    u, w = basis(d)
    def rr(p, r):
        return [m.vert(*[p[k] + r * (math.cos(2 * math.pi * i / n) * u[k] + math.sin(2 * math.pi * i / n) * w[k])
                         for k in range(3)]) for i in range(n)]
    a, b = rr(p0, r0), rr(p1, r1)
    ctr = [(p0[k] + p1[k]) / 2 for k in range(3)]
    band(m, a, b, ctr)
    if caps[0]:
        cap(m, rr(p0, r0), p0, [p0[k] + d[k] for k in range(3)])
    if caps[1]:
        cap(m, rr(p1, r1), p1, [p1[k] - d[k] for k in range(3)])





# ---------------------------------------------------------------------------
# The room's things. Sizes and places are MEASURED off the two backgrounds
# (rays cast through camera 0's and camera 1's pictures onto the floor, the
# walls and the rims' planes); the numbers in each docstring are those.
# ---------------------------------------------------------------------------

BATH = dict(a=1465.0, b=800.0, y_base=-150.0, y_rim=-1080.0, depth=620.0, wall=85.0,
            n=48, power=5.0, flare=(0.93, 0.99), inner_floor=0.86, lip=25.0)


def bath_outer():
    """The bath, outside: 2930 x 1600, its rolled rim at 1080 - dark, as
    camera 1 sees it - on four short feet."""
    m = Mesh()
    vessel(m, 0.0, 0.0, part="outer", **BATH)
    for sx in (-1, 1):
        for sz in (-1, 1):
            x, z = sx * 1100.0, sz * 560.0
            tube(m, (x, -170, z), (x + sx * 30, 0, z + sz * 30), 80, 60, 8)
    m.save("m1.obj")


def bath_inner():
    """The bath's rim and hollow: the pale green enamel camera 0 looks into."""
    m = Mesh()
    vessel(m, 0.0, 0.0, part="inner", **BATH)
    m.save("m11.obj")


def toilet():
    """The pan: an oval bowl on a narrow foot; its back toward +Z (the wall).
    Its seat measures 810 x 990 at 450."""
    m = Mesh()
    vessel(m, 0.0, 0.0, 400.0, 500.0, -260.0, -430.0, 300.0, 45.0, n=24, power=2.2,
           flare=(0.70, 0.92), inner_floor=0.55, lip=10.0)
    f0 = ring(m, 0.0, 120.0, 200.0, 260.0, 0.0, 16, 2.2)
    f1 = ring(m, 0.0, 60.0, 260.0, 340.0, -260.0, 16, 2.2)
    band(m, f0, f1, (0.0, -130.0, 90.0))
    box(m, -230, -560, 420, 230, -300, 640, 2)          # the back block the seat hinges on
    m.save("m2.obj")


def toilet_lid():
    """The lid, closed: rusted, a little larger than the bowl."""
    m = Mesh()
    top = ring(m, 0.0, -20.0, 410.0, 500.0, -500.0, 28, 2.2)
    bot = ring(m, 0.0, -20.0, 410.0, 500.0, -440.0, 28, 2.2)
    band(m, bot, top, (0.0, -470.0, -20.0))
    cap(m, top, (0.0, -500.0, -20.0), (0.0, -470.0, -20.0))
    m.save("m3.obj")


def toilet_hinges():
    """The lid's chrome: the hinges at its back and the thin band round its
    edge the repaint shows."""
    m = Mesh()
    for x in (-150, 150):
        tube(m, (x - 50, -520, 470), (x + 50, -520, 470), 28, None, 8)
    rr = [ring(m, 0.0, -20.0, 416.0 + d, 506.0 + d, y, 32, 2.2) for d, y in ((0, -495), (6, -470), (0, -445))]
    band(m, rr[0], rr[1], (0.0, -470.0, -20.0))
    band(m, rr[1], rr[2], (0.0, -470.0, -20.0))
    m.save("m28.obj")


def cistern_fittings():
    """The low cistern box is a tbox (x 6680..7880, y -1800..-900, z
    10160..10728); these are its brass: the flush lever on its low-X side and
    the bent pipe down into the pan's side. Placed at the box's middle."""
    m = Mesh()
    tube(m, (-600, -1450, -120), (-700, -1450, -120), 30, None, 8)
    tube(m, (-700, -1450, -120), (-720, -1300, -200), 22, 16, 8)
    # the flush pipe: out of the box's underside, down, and round into the
    # left side of the pan's back block (x 7170, z 9950..10170), as camera 1
    # shows it curving in at the seat's height
    pts = [(-330, -900, -195), (-345, -640, -270), (-290, -480, -360), (-100, -445, -380)]
    for a, b in zip(pts, pts[1:]):
        tube(m, a, b, 32, 32, 10)
    m.save("m4.obj")


def sink():
    """The wall-hung basin on the high-X wall (+X): a rounded oblong 920 x
    1040, its rim at 1250, on a bracket."""
    m = Mesh()
    vessel(m, 0.0, 0.0, 460.0, 520.0, -1000.0, -1250.0, 230.0, 60.0, n=32, power=5.0,
           flare=(0.80, 0.95), inner_floor=0.72, lip=18.0)
    box(m, 340, -1000, -80, 460, -700, 80, 2)
    m.save("m5.obj")


def sink_shelf():
    """The enamel shelf under the mirror, 300 deep x 1155: a thin flat plate with
    a low raised rim round it, as camera 0 sees it. The rays put it at 2107;
    it is set 130 lower so its rim meets the mirror's lower edge (2010)
    instead of standing in front of the glass. Narrower than the rays say
    (575), so it does not hide the basin's taps from the shoulder camera."""
    m = Mesh()
    cx, cz, a, b, y = 0.0, 0.0, 150.0, 577.0, -1976.0
    n, pw, rim, th = 32, 8.0, 34.0, 30.0
    o_top = ring(m, cx, cz, a, b, y - rim, n, pw)            # the rim's top, outside edge
    o_bot = ring(m, cx, cz, a, b, y + th, n, pw)             # the plate's underside edge
    i_top = ring(m, cx, cz, a - 26, b - 26, y - rim, n, pw)
    i_bot = ring(m, cx, cz, a - 26, b - 26, y, n, pw)        # where the rim meets the plate
    mid = (cx, y, cz)
    band(m, o_bot, o_top, mid)                                # outside
    band(m, o_top, i_top, (cx, y + 400, cz))                  # the rim's top, facing up
    for i in range(n):                                        # the rim's inside, facing in
        k = (i + 1) % n
        A = m.v[i_top[i]]
        m.quad(i_top[i], i_top[k], i_bot[k], i_bot[i], (cx + (A[0] - cx) * 2, A[1], cz + (A[2] - cz) * 2))
    cap(m, i_bot, (cx, y, cz), (cx, y + 10, cz))              # the plate, facing up
    cap(m, o_bot, (cx, y + th, cz), (cx, y, cz))              # its underside
    m.save("m6.obj")


def bottle():
    """The white bottle on that shelf."""
    m = Mesh()
    r = [ring(m, 0.0, 0.0, rr, rr, y, 14, 2.0) for rr, y in
         ((75, 0), (78, -60), (78, -260), (45, -320), (32, -340), (32, -390))]
    for r0, r1 in zip(r, r[1:]):
        band(m, r0, r1, (0.0, -200.0, 0.0))
    cap(m, r[0], (0.0, 0.0, 0.0), (0.0, -10.0, 0.0))
    cap(m, r[-1], (0.0, -390.0, 0.0), (0.0, -380.0, 0.0))
    m.save("m12.obj")


def towel_rack():
    """The towel horse, LEANING on the far wall as camera 0 shows it: its top
    rail against the wall (z -50 here), its two legs - feet cast onto the
    floor at x 6084 and 7120, z 6210 - reaching 560 out into the room. Placed at the rail's middle."""
    m = Mesh()
    for x in (-518.0, 518.0):
        tube(m, (x, -1030, -50), (x, 0, 510), 28, 24, 6)           # a leg
    box(m, -560, -1030, -85, 560, -970, -15, 4)                  # the top rail
    m.save("m7.obj")


def drape(m, prof, half, nx, pleats, amp, th):
    """A cloth hung over something: the cross-section `prof` ((z, y) points,
    in order along the cloth) swept 2*half along X, `th` thick, with soft
    vertical pleats of depth `amp` where the cloth hangs free. A closed slab:
    two skins, side edges and hems."""
    K = len(prof)
    nrm = []
    for k in range(K):
        a = prof[max(0, k - 1)]
        b = prof[min(K - 1, k + 1)]
        tz, ty = b[0] - a[0], b[1] - a[1]
        L = math.hypot(tz, ty) or 1.0
        nrm.append((ty / L, -tz / L))
    free = [min(1.0, max(0.0, (y - min(p[1] for p in prof)) / 300.0)) for _, y in prof]
    def skin(off):
        g = []
        for i in range(nx + 1):
            t = i / nx
            x = -half + 2 * half * t
            pl = amp * abs(math.sin(pleats * math.pi * t))
            row = []
            for k, (z, y) in enumerate(prof):
                d = off + pl * free[k]
                row.append(m.vert(x, y + nrm[k][1] * d, z + nrm[k][0] * d))
            g.append(row)
        return g
    out, inn = skin(0.0), skin(th)
    mid = lambda ids: [sum(m.v[i][c] for i in ids) / len(ids) for c in range(3)]
    for i in range(nx):
        for k in range(K - 1):
            m.quad(out[i][k], out[i + 1][k], out[i + 1][k + 1], out[i][k + 1],
                   mid([inn[i][k], inn[i + 1][k + 1]]))
            m.quad(inn[i][k], inn[i + 1][k], inn[i + 1][k + 1], inn[i][k + 1],
                   mid([out[i][k], out[i + 1][k + 1]]))
    for i in (0, nx):
        for k in range(K - 1):
            c = mid([out[i][k], out[i][k + 1], inn[i][k], inn[i][k + 1]])
            c[0] += 20 if i == 0 else -20
            m.quad(out[i][k], out[i][k + 1], inn[i][k + 1], inn[i][k], c)
    for k, kn in ((0, 1), (K - 1, K - 2)):
        for i in range(nx):
            c = mid([out[i][k], out[i + 1][k], inn[i][k], inn[i + 1][k]])
            nb = mid([out[i][kn], inn[i][kn]])
            c = [c[j] + (nb[j] - c[j]) * 0.2 for j in range(3)]
            m.quad(out[i][k], out[i + 1][k], inn[i + 1][k], inn[i][k], c)


def towel():
    """The towel over the rack's top rail (rail at 1000, 70 thick, z -50):
    folded over it and falling both sides to about 480, in soft pleats -
    camera 0's blue-grey cloth."""
    m = Mesh()
    prof = [(-108, -470), (-106, -760), (-102, -960), (-84, -1045), (-50, -1062),
            (-16, -1045), (2, -960), (6, -760), (8, -520)]
    drape(m, prof, 500.0, 16, 2, 22.0, 22.0)
    m.save("m8.obj", double=True)


def bath_taps():
    """The mixer at the bath's head, placed at its middle (2320, 10000): a
    bar along the head (z 9720..10290, at 1300 - camera 0 and camera 1
    triangulate its end to (2319, -1304, 9723)) with a cross-handled valve at
    each end, held up by a riser from the floor outside the bath, and a short
    spout out of its middle into the bath. Tarnished brass."""
    m = Mesh()
    tube(m, (0, -1300, -285), (0, -1300, 285), 30, None, 12)          # the bar
    for z in (-285.0, 285.0):
        tube(m, (0, -1300, z), (0, -1300, z + (-40 if z < 0 else 40)), 44, 40, 12)   # valve bodies
        tube(m, (0, -1300, z), (0, -1380, z), 16, None, 8)            # stems
        box(m, -75, -1400, z - 13, 75, -1380, z + 13)                 # cross handles
        box(m, -13, -1400, z - 75, 13, -1380, z + 75)
    tube(m, (-120, 0, 0), (-120, -1300, 0), 28, None, 10, (False, True))   # the riser
    tube(m, (-120, -1300, 0), (0, -1300, 0), 28, None, 10)
    tube(m, (0, -1300, 0), (230, -1290, 0), 24, 20, 10)               # the short spout
    tube(m, (230, -1290, 0), (270, -1240, 0), 20, 18, 8)
    m.save("m9.obj")


def shower():
    """The hand shower: a nickel pipe out of the mixer's CENTRE body - the
    diverter under the spout, where a period bath mixer takes its hand
    shower; camera 1 shows its curve leaving the middle of the bar, below it -
    dropping and arcing over into the bath, the rose down on the bath's floor.
    A smooth curve, so it bends rather than kinks. (The dark arc beside it in
    camera 0's picture is its shadow on the enamel, not a hose.)"""
    m = Mesh()
    tube(m, (0.0, -1300.0, 0.0), (0.0, -1215.0, 0.0), 34, 30, 10)     # the diverter, under the spout
    P0, P1, P2, P3 = (0.0, -1215.0, 0.0), (40.0, -1080.0, 120.0), (600.0, -1250.0, -240.0), (1060.0, -580.0, 170.0)
    pts = []
    for k in range(14):
        t = k / 13.0
        a, b, c, d = (1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t * t * (1 - t), t ** 3
        pts.append(tuple(a * P0[i] + b * P1[i] + c * P2[i] + d * P3[i] for i in range(3)))
    for a, b in zip(pts, pts[1:]):
        tube(m, a, b, 20, 20, 8)
    tube(m, pts[-1], (1120.0, -500.0, 260.0), 40, 52, 10)              # the rose, down on the floor
    m.save("m25.obj")


def basin_taps():
    """The basin's two pillar taps, standing on its back rim (camera 0 sees
    them inside its wall-side edge), and its waste; placed on the high-X wall
    (-X is the room). The basin's middle is 458 off the wall, its rim at 1250."""
    m = Mesh()
    for z in (-210.0, 210.0):
        tube(m, (-75, -1250, z), (-75, -1420, z), 30, 26, 10)          # the pillar
        tube(m, (-75, -1400, z), (-190, -1400, z), 20, 18, 8)          # the nozzle
        tube(m, (-190, -1400, z), (-205, -1360, z), 18, 16, 8)
        tube(m, (-75, -1420, z), (-75, -1460, z), 14, None, 6)
        box(m, -130, -1480, z - 11, -20, -1460, z + 11)                # cross handle
        box(m, -86, -1480, z - 55, -64, -1460, z + 55)
    tube(m, (-458, -1010, 0), (-458, -760, 0), 34, None, 10)
    tube(m, (-458, -760, 0), (-400, -680, 0), 34, None, 10)
    tube(m, (-400, -680, 0), (0, -680, 0), 34, None, 10)
    tube(m, (0, -680, 0), (-10, -680, 0), 60, None, 10)
    m.save("m10.obj")


def trough():
    """The long enamel tray on the near wall over the bath, 1130 x 456 at
    2150 - pale inside, a deep belly under it that sits on the rail's cap, as
    camera 1 sees it (no brackets: the rail carries it)."""
    m = Mesh()
    vessel(m, 0.0, 0.0, 565.0, 228.0, -1880.0, -2150.0, 140.0, 30.0, n=28, power=4.0,
           flare=(0.70, 0.90), inner_floor=0.80, lip=10.0)
    m.save("m13.obj")


def soap():
    """The brown glass jar in the trough over the bath (the RE-ENHANCE
    repaint shows it square, amber, its lid rusted): 170 square, 150 tall,
    rounded at the edges, and a low lid."""
    m = Mesh()
    lo = ring(m, 0.0, 0.0, 85.0, 85.0, 0.0, 20, 6.0)
    hi = ring(m, 0.0, 0.0, 85.0, 85.0, -140.0, 20, 6.0)
    band(m, lo, hi, (0.0, -70.0, 0.0))
    cap(m, lo, (0.0, 0.0, 0.0), (0.0, -10.0, 0.0))
    lid0 = ring(m, 0.0, 0.0, 78.0, 78.0, -140.0, 20, 6.0)
    lid1 = ring(m, 0.0, 0.0, 78.0, 78.0, -175.0, 20, 6.0)
    band(m, lid0, lid1, (0.0, -158.0, 0.0))
    cap(m, lid1, (0.0, -175.0, 0.0), (0.0, -165.0, 0.0))
    m.save("m14.obj")


def head_tray():
    """The enamel tray on the wall at the bath's head (low X): 400 out from
    the wall x 1090 along it, its rim at 2220 - camera 0 and camera 1 agree
    on all three."""
    m = Mesh()
    vessel(m, 0.0, 0.0, 200.0, 545.0, -2140.0, -2220.0, 50.0, 22.0, n=28, power=6.0,
           flare=(0.92, 0.98), inner_floor=0.92, lip=8.0)
    m.save("m15.obj")


def bath_towel():
    """The towel over the bath's long rim: 1140 wide, a short fall inside, and
    outside down to the floor in three soft pleats - what camera 0 shows as
    three rolls are their tops. A thin closed slab; the rim's outer edge is
    at z 0, the bath toward +z."""
    m = Mesh()
    # Clear of the rim: the bath's rolled lip stands to y -1092 and out to
    # z -25 here, and the cloth's thickness hangs below this profile.
    prof = [(230, -1040), (180, -1130), (100, -1172), (20, -1180), (-50, -1145),
            (-80, -1050), (-86, -780), (-90, -430), (-92, -110)]
    nx, half, th = 18, 570.0, 28.0
    def surf(off):
        g = []
        for i in range(nx + 1):
            t = i / nx
            x = -half + 2 * half * t
            pleat = 38.0 * abs(math.sin(3 * math.pi * t))
            row = []
            for k, (z, y) in enumerate(prof):
                zz = z - pleat * (1.0 if k >= 4 else 0.4) + off
                row.append(m.vert(x, y + (off * 0.4 if k < 5 else 0.0), zz))
            g.append(row)
        return g
    out, inn = surf(0.0), surf(th)
    K = len(prof)
    for i in range(nx):
        for k in range(K - 1):
            c_in = [(m.v[inn[i][k]][c] + m.v[inn[i + 1][k + 1]][c]) / 2 for c in range(3)]
            c_out = [(m.v[out[i][k]][c] + m.v[out[i + 1][k + 1]][c]) / 2 for c in range(3)]
            m.quad(out[i][k], out[i + 1][k], out[i + 1][k + 1], out[i][k + 1], c_in)
            m.quad(inn[i][k], inn[i + 1][k], inn[i + 1][k + 1], inn[i][k + 1], c_out)
    for i in (0, nx):                                         # the side edges
        for k in range(K - 1):
            ctr = [sum(m.v[g[i][kk]][c] for g in (out, inn) for kk in (k, k + 1)) / 4 for c in range(3)]
            ctr[0] += 20 if i == 0 else -20
            m.quad(out[i][k], out[i][k + 1], inn[i][k + 1], inn[i][k], ctr)
    for k in (0, K - 1):                                      # the hems
        for i in range(nx):
            ctr = [sum(m.v[g[ii][k]][c] for g in (out, inn) for ii in (i, i + 1)) / 4 for c in range(3)]
            ctr[1] += -20 if k == K - 1 else 20
            m.quad(out[i][k], out[i + 1][k], inn[i + 1][k], inn[i][k], ctr)
    m.save("m16.obj", double=True)


def jug():
    """What stands in the tray at the bath's head. Neither picture says for
    certain: camera 1 sees a white bell ~300 tall with deep vertical folds and
    a short neck sticking out of its top; the repaint, from above, a knob with
    folds fanning out from it. Both fit a white cloth hung over a bottle - so
    that is what this is: the bottle's neck, and the cloth falling from it in
    folds to the tray."""
    m = Mesh()
    n = 24
    prof = [(130, 0), (128, -60), (118, -140), (96, -210), (62, -255), (34, -275)]
    rings = []
    for rr, y in prof:
        r = []
        for i in range(n):
            t = 2 * math.pi * i / n
            fold = 1.0 + 0.18 * math.cos(5 * t) * (y / -275.0 * 0.4 + 0.6)   # the folds, deepest low down
            r.append(m.vert(rr * fold * math.cos(t), y, rr * fold * math.sin(t)))
        rings.append(r)
    for r0, r1 in zip(rings, rings[1:]):
        band(m, r0, r1, (0.0, -140.0, 0.0))
    cap(m, rings[0], (0.0, 0.0, 0.0), (0.0, -10.0, 0.0))
    cap(m, rings[-1], (0.0, -275.0, 0.0), (0.0, -265.0, 0.0))
    tube(m, (0, -270, 0), (0, -330, 0), 22, 20, 10)                   # the bottle's neck, out of the top
    m.save("m17.obj")


def brush():
    """The safety razor standing in the trough: a knurled handle and its head."""
    m = Mesh()
    tube(m, (0, 0, 0), (0, -170, 0), 16, 16, 8)                      # the handle
    tube(m, (0, -170, 0), (0, -190, 0), 22, 22, 8)
    box(m, -70, -215, -18, 70, -190, 18, 1)                          # the head
    m.save("m18.obj")


def paper_holder():
    """A toilet-paper holder; the wall at z 0, the room toward -z."""
    m = Mesh()
    box(m, -110, -50, -15, 110, 50, 0, 1)
    for x in (-100, 100):
        box(m, x - 10, -15, -150, x + 10, 15, -15, 1)
    tube(m, (-100, 0, -135), (100, 0, -135), 10, None, 6)
    m.save("m19.obj")


def paper_roll():
    m = Mesh()
    tube(m, (-80, 0, -135), (80, 0, -135), 68, None, 14)
    m.save("m20.obj")


def door_knob():
    """The door's knob, on the room side of its brass plate."""
    m = Mesh()
    tube(m, (0, 0, 0), (0, 0, 70), 16, None, 8)
    tube(m, (0, 0, 70), (0, 0, 120), 46, 40, 12)
    m.save("m24.obj")


def mirror_frame():
    """The mirror's wooden frame, on the high-X wall (the room toward -X),
    round a glass 840 x 1050: the hole the level leaves in the wall, through
    which the reflection (RaidArena.cpp, RaDrawMirror) shows."""
    m = Mesh()
    zo, zi, yt, yb, d = 490.0, 420.0, -3200.0, -2010.0, -50.0
    box(m, d, yt, -zo, 0, yt + 70, zo, 3)
    box(m, d, yb - 70, -zo, 0, yb, zo, 3)
    box(m, d, yt + 70, -zo, 0, yb - 70, -zi, 3)
    box(m, d, yt + 70, zi, 0, yb - 70, zo, 3)
    m.save("m27.obj")


def stop_valve():
    """The stop valve low on the near wall by the cistern: a chrome elbow out
    of the wall and its little wheel; the wall at z 0, the room toward -z."""
    m = Mesh()
    tube(m, (0, 0, 0), (0, 0, -60), 40, 34, 10)
    tube(m, (0, 0, -60), (0, 0, -120), 30, 30, 10)
    tube(m, (0, 0, -120), (0, 150, -120), 24, 24, 8, (True, False))
    tube(m, (0, -10, -90), (0, -70, -90), 10, 10, 6)
    tube(m, (-40, -80, -90), (40, -80, -90), 12, 12, 6)
    tube(m, (0, -80, -130), (0, -80, -50), 12, 12, 6)
    m.save("m29.obj")


DOOR_W = 1400.0     # wider than the picture's 1180: the user's call; the picture is stretched to it


def door_leaf():
    """The bathroom door as a model of its own, so it can swing (`door` line,
    RaidArena.cpp): a leaf DOOR_W x 3195, 46 thick, its origin at the HINGE
    (low X, the floor, the middle of its thickness), shut along +X. Its
    picture (t5) is laid on it in model space (mesh flag 512), so it turns
    with it. The four panels stand a little proud of the stiles, where the
    picture has them."""
    m = Mesh()
    W, H, T = DOOR_W, 3195.0, 23.0
    k = W / 1180.0
    box(m, 0, -H, -T, W, 0, T, 1)
    for x0, x1 in ((130 * k, 480 * k), (535 * k, 922 * k)):
        for y0, y1 in ((-2992, -1553), (-1203, -207)):
            box(m, x0, y0, -T - 10, x1, y1, -T, 1)
            box(m, x0, y0, T, x1, y1, T + 10, 1)
    m.save("m30.obj")


def door_knobs():
    """Its brass, the only knob it has (painted out of the picture): a plate
    and a knob each side, 140 in from the leaf's free edge and 1243 up, and
    three hinge knuckles at the hinge."""
    m = Mesh()
    kx = DOOR_W - 140.0
    for s_ in (-1, 1):
        box(m, kx - 40, -1360, s_ * 23, kx + 40, -1130, s_ * 31)
        tube(m, (kx, -1243, s_ * 31), (kx, -1243, s_ * 85), 14, None, 8)
        tube(m, (kx, -1243, s_ * 85), (kx, -1243, s_ * 125), 44, 38, 12)
    for y in (-2900, -1600, -300):
        tube(m, (-6, y - 110, 0), (-6, y + 110, 0), 20, None, 8)
    m.save("m31.obj")

if __name__ == "__main__":
    for name in os.listdir(OUT):
        if name.endswith(".obj"):
            os.remove(os.path.join(OUT, name))
    for f in (bath_outer, bath_inner, toilet, toilet_lid, cistern_fittings, sink, sink_shelf,
              bottle, towel_rack, towel, bath_taps, basin_taps, trough, soap, head_tray,
              bath_towel, jug, brush, paper_holder, paper_roll, door_knob, shower,
              mirror_frame, toilet_hinges, stop_valve,
              door_leaf, door_knobs):
        f()
