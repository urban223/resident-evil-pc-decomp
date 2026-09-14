#!/usr/bin/env python3
"""build_raid_bgm.py - the EXTRA (RAID) screen's music bed.

A seamless 8-bar loop, synthesised here for the same reason the sting is: the
screen it plays under is built from frame numbers, and a file generated from
those numbers cannot drift out of step with them.

The shape is not guessed. Both reference tracks were measured (onset-comb for
tempo, Krumhansl profiles on a chroma sum for key, per-bar triad matching for
harmony, band RMS for the balance):

  Frontier Conquest menu theme  100.0 BPM  C minor   sub 53 / low 32 / mid 11 /
                                                     hi 3.5 / air 1
  One Hit Kill                  115.0 BPM  D minor   sub 57 / low 26 / mid 10 /
                                                     hi 6 / air 2

The menu theme is the base, so this takes its tempo, its key and its harmonic
family: i - bVII - bVI - v, two bars each, which is the pattern its own chord
track walks. One Hit Kill is the flavour, and what it contributes is its
STATICNESS and its drive - it sits on one chord for four bars at a time under a
sixteenth bass, which is the one thing that makes a loop this long bearable.
Both are heavily bottom-weighted; the balance below is aimed between them.

Output: assets/USA/Sound/raidbgm.wav, mono / 22050 Hz / 16-bit PCM - the format
every shipped RE1 sound uses. Played with playSnd(bank, 1); slot != 0 is what
sets XAUDIO2_LOOP_INFINITE, which repeats the WHOLE buffer, so the file itself
has to be the loop - there is no loop-region mechanism anywhere in this build.

The loop is rendered at DOUBLE length and folded in half at the end
(out[:L] += out[L:]), which wraps every reverb and delay tail round to the
front. Rendering one length and hoping is what produces the click.

Run: python3 tools/build_raid_bgm.py
"""

import os
import wave

import numpy as np

RATE = 22050
BPM = 100.0                  # the menu theme's own tempo, measured
BEAT = 60.0 / BPM            # 0.6 s
BAR = BEAT * 4               # 2.4 s
BARS = 8
LOOP = BAR * BARS            # 19.2 s - 423360 samples, no rounding
STEP = BEAT / 4              # a sixteenth

N = int(RATE * LOOP)
NN = N * 2                   # the render length, folded at the end

rng = np.random.default_rng(0x8A1D)

HIGH_SHELF = 1.30      # see the note at the shelf, in main()


def note(name, octave):
    """Equal temperament, A4 = 440."""
    semis = {"C": -9, "C#": -8, "D": -7, "D#": -6, "E": -5, "F": -4,
             "F#": -3, "G": -2, "G#": -1, "A": 0, "A#": 1, "B": 2}[name]
    return 440.0 * (2.0 ** (semis / 12.0 + (octave - 4)))


def saw(freq, n, detune=0.0, phase=0.0):
    """Additive saw, band-limited to Nyquist.

    At 22050 Hz a naive saw folds everything above 11 kHz back down as
    inharmonic noise, and on a bass line that is audible as grit rather than as
    brightness. Summing only the partials that fit costs a few dozen sines and
    is simply correct.
    """
    t = np.arange(n) / RATE
    f = freq * (2.0 ** (detune / 1200.0))
    out = np.zeros(n)
    k = 1
    while f * k < RATE * 0.48 and k <= 48:
        out -= np.sin(2 * np.pi * f * k * t + phase) / k
        k += 1
    return out * (2.0 / np.pi)


def adsr(n, a, d, s, r):
    e = np.zeros(n)
    ai, di, ri = int(a * RATE), int(d * RATE), int(r * RATE)
    ai = min(ai, n)
    e[:ai] = np.linspace(0, 1, ai) if ai else 0
    di = min(di, n - ai)
    if di > 0:
        e[ai:ai + di] = np.linspace(1, s, di)
    if ai + di < n:
        e[ai + di:] = s
    if ri > 0:
        ri = min(ri, n)
        e[-ri:] *= np.linspace(1, 0, ri)
    return e


def lp1(x, cutoff):
    """One-pole low-pass. `cutoff` may be a scalar or per-sample (the sweep)."""
    c = np.atleast_1d(cutoff)
    if c.size == 1:
        c = np.full(len(x), c[0])
    k = 1.0 - np.exp(-2.0 * np.pi * np.clip(c, 20.0, RATE * 0.45) / RATE)
    y = np.empty(len(x))
    acc = 0.0
    for i in range(len(x)):
        acc += (x[i] - acc) * k[i]
        y[i] = acc
    return y


def lp1_wrap(x, cutoff):
    """lp1 on a LOOP: filtered as if the loop had already been playing.

    A causal filter starts from zero, so its first samples are a transient and
    its last are not the state the first ones should have followed. On a
    one-shot that is inaudible; on a loop it IS the seam - the master shelf here
    put a 0.43 step at the join before this existed. Filtering two copies and
    keeping the second gives the steady state at the loop point.
    """
    n = len(x)
    return lp1(np.concatenate([x, x]), cutoff)[n:]


def place(buf, sig, sec, gain=1.0):
    i = int(sec * RATE)
    n = min(len(sig), len(buf) - i)
    if n > 0:
        buf[i:i + n] += sig[:n] * gain


# ---------------------------------------------------------------------------
# the progression: Cm - Bb - Ab - Gm, two bars each
#
# C minor and this family are the menu theme's, read off its own chord track.
# All four are diatonic to natural minor and none of them is a dominant - there
# is no leading note anywhere in it, which is what keeps a loop from announcing
# its own seam every nineteen seconds. It ends on the minor v, the softest
# possible way back to the top.
# ---------------------------------------------------------------------------
CM  = ("C",  ["C", "D#", "G"])
BB  = ("A#", ["A#", "D", "F"])
AB  = ("G#", ["G#", "C", "D#"])
GM  = ("G",  ["G", "A#", "D"])

# Three bars of tonic before it moves. Two bars each was harmonically tidy and
# measured as G MINOR, not C - a bass hammering roots puts the chroma weight
# wherever it spends its time, and an even split spends as long on the v as on
# the i. The menu theme does not do that either: its own chord track sits on C
# for five windows, walks Bb/Gm/Ab, and comes back. This is that shape.
PROG = [CM, CM, CM, BB, AB, AB, BB, GM]


def main():
    out = np.zeros(NN)

    # --- pad: three detuned saws per note, slow, heavily filtered -----------
    pad = np.zeros(NN)
    for b, (root, chord) in enumerate(PROG):
        n = int(BAR * 1.35 * RATE)          # overlaps the next bar: no gaps
        env = adsr(n, 0.35, 0.5, 0.75, 0.9)
        v = np.zeros(n)
        for name in chord:
            # An octave BELOW the lead. At octave 4 the pad sat in exactly the
            # band the line occupies and, being sustained, was the most salient
            # thing in it from end to end - the detector read the pad as the
            # melody. Pad under, line in the middle, arp above: the three of
            # them each get a register instead of sharing one.
            f = note(name, 3)
            for det in (-9.0, 0.0, 9.0):
                v += saw(f, n, det) * 0.33
        place(pad, v * env, b * BAR)
    # a slow sweep over the whole loop, which is the genre's signature move
    t = np.arange(NN) / RATE
    sweep = 420.0 + 380.0 * (1.0 - np.cos(2 * np.pi * t / LOOP)) * 0.5
    out += lp1(pad, sweep) * 0.30

    # --- bass: sixteenths, root with octave jumps --------------------------
    bass = np.zeros(NN)
    pattern = [0, 0, 12, 0, 0, 12, 0, 0, 0, 12, 0, 0, 12, 0, 12, 0]
    for b, (root, _) in enumerate(PROG):
        f0 = note(root, 2)
        for s, semi in enumerate(pattern):
            n = int(STEP * 1.6 * RATE)
            f = f0 * (2.0 ** (semi / 12.0))
            env = adsr(n, 0.004, 0.06, 0.55, 0.07)
            place(bass, saw(f, n) * env, b * BAR + s * STEP,
                  0.9 if s % 4 == 0 else 0.62)
    # Two passes, not one. A one-pole rolls off 6 dB an octave, which at 1 kHz
    # still leaves a sixteenth-note saw bass loud enough to be the most salient
    # thing in the LEAD's register - run the same note detector over the mix and
    # it reports the bass's harmonics as the melody, five notes a bar of it.
    # Twelve dB an octave clears the band for the line that belongs in it.
    bcut = 300.0 + 260.0 * (1.0 - np.cos(2 * np.pi * t / (LOOP / 2))) * 0.5
    out += lp1(lp1(bass, bcut), bcut) * 0.72

    # --- arp: plucked sixteenths, up the chord -----------------------------
    #
    # Only on half the bars. A menu bed is something you sit in front of for
    # minutes at a time, and sixteenths running through every one of those bars
    # stops being a texture and starts being a demand.
    arp = np.zeros(NN)
    for b, (_, chord) in enumerate(PROG):
        if b not in (1, 3, 5, 7):
            continue
        seq = [chord[0], chord[1], chord[2], chord[1]] * 4
        for s, name in enumerate(seq):
            # An octave above the lead, deliberately. Measured through the same
            # note detector as the reference, the arp at 4/5 landed INSIDE the
            # lead's register (F4..D5) and the line came back as five notes a
            # bar instead of two and a half - the arp was being heard as part of
            # the melody. Above it, it reads as shimmer and the line stays the
            # line.
            oct_ = 6 if s % 8 >= 4 else 5
            n = int(STEP * 1.1 * RATE)
            env = adsr(n, 0.002, 0.09, 0.0, 0.03)
            place(arp, saw(note(name, oct_), n, 5.0) * env,
                  b * BAR + s * STEP, 0.5 if s % 2 else 0.85)
    arp = lp1(arp, 2600.0)
    # dotted-eighth delay, twice - the thing that makes an arp sound wide
    for d, g in ((BEAT * 0.75, 0.42), (BEAT * 1.5, 0.18)):
        place(arp, arp[:NN - int(d * RATE)].copy(), d, g)
    out += arp * 0.15

    # --- lead ---------------------------------------------------------------
    #
    # Written to the menu theme's melodic HABITS, measured off it rather than
    # copied from it - the pitches here are not its pitches:
    #
    #   2.44 notes a bar, and sounding only 39% of the time. The line is more
    #     rest than note, which is why it never wears out over a long sit.
    #   median note 0.4 beats, quartiles 0.3 and 0.8 - short, almost never held
    #     across a bar line.
    #   register MIDI 65..74 (F4..D5), a nine-semitone span. Mid, not high: it
    #     sits INSIDE the pad rather than over it.
    #   intervals 59% of two semitones or less, 31% of a fourth to a fifth,
    #     almost no thirds. Stepwise with the occasional open leap, which is the
    #     shape that gives it its character.
    #
    # Two four-bar phrases, the second answering the first and ending on the
    # minor v, unresolved, so the loop turns over without a full stop.
    # (beat from the top of the loop, note, octave, length in beats)
    lead = np.zeros(NN)
    MEL = [
        ( 1.5, "G",  4, 0.5), ( 2.0, "A#", 4, 0.5), ( 2.5, "C",  5, 1.0),
        ( 6.0, "G",  4, 0.5), ( 6.5, "F",  4, 0.75),
        ( 9.5, "C",  5, 0.5), (10.0, "D",  5, 0.5), (10.5, "C",  5, 1.0),
        (12.5, "A#", 4, 0.5), (13.5, "G",  4, 1.25),

        (17.5, "G#", 4, 0.5), (18.0, "G",  4, 0.5), (18.5, "C",  5, 1.0),
        (22.0, "A#", 4, 0.5), (22.5, "G#", 4, 0.75),
        (25.5, "A#", 4, 0.5), (26.0, "C",  5, 0.5), (26.5, "G",  4, 1.0),
        (28.5, "F",  4, 0.5), (29.5, "G",  4, 1.5),
    ]
    for beat, name, oct_, dur in MEL:
        n = int(dur * BEAT * 1.25 * RATE)      # a little overlap, not a legato
        env = adsr(n, 0.015, 0.18, 0.62, 0.30)
        f = note(name, oct_)
        v = (saw(f, n, -7.0) + saw(f, n, 7.0)) * 0.5
        place(lead, lp1(v, 2600.0) * env, beat * BEAT)
    for d, g in ((BEAT * 0.75, 0.38), (BEAT * 1.5, 0.16)):
        place(lead, lead[:NN - int(d * RATE)].copy(), d, g)
    out += lead * 0.42

    # --- drums --------------------------------------------------------------
    def kick(n):
        h = np.arange(n) / RATE
        f = 105.0 * np.exp(-h * 32.0) + 44.0
        return (np.sin(2 * np.pi * np.cumsum(f) / RATE) *
                np.exp(-h * 9.0) * np.clip(h / 0.002, 0, 1))

    def clap(n):
        h = np.arange(n) / RATE
        body = rng.standard_normal(n) * np.exp(-h * 26.0)
        # gated reverb: a noise tail cut off dead, which is the sound of 1984
        tail = rng.standard_normal(n) * np.exp(-h * 5.0) * 0.55
        gate = (h < 0.16).astype(float)
        gate *= np.clip((0.16 - h) / 0.03, 0, 1)
        return lp1(body + tail * gate, 3400.0)

    def hat(n, open_=False):
        h = np.arange(n) / RATE
        d = 5.0 if open_ else 42.0
        x = rng.standard_normal(n) * np.exp(-h * d)
        return x - lp1(x, 4200.0)          # crude high-pass

    for b in range(BARS):
        place(out, kick(int(0.45 * RATE)), b * BAR, 0.62)
        place(out, kick(int(0.45 * RATE)), b * BAR + BEAT * 2, 0.62)
        if b % 4 == 3:
            place(out, kick(int(0.30 * RATE)), b * BAR + BEAT * 3.5, 0.42)
        place(out, clap(int(0.40 * RATE)), b * BAR + BEAT * 2, 0.22)
        for s in range(8):
            op = (s == 7 and b % 2 == 1)
            n = int((0.26 if op else 0.07) * RATE)
            # Hats are broadband, and broadband at eight a bar buries everything
            # with a pitch in it. Kept well under the bass and the pad.
            place(out, hat(n, op), b * BAR + s * STEP * 2,
                  0.075 if op else (0.058 if s % 2 == 0 else 0.036))

    # --- a riser over the last half bar, which lands on the loop point ------
    rn = int(BEAT * 2 * RATE)
    r = rng.standard_normal(rn)
    ramp = (np.arange(rn) / rn) ** 2.5
    place(out, lp1(r, 300.0 + 5200.0 * ramp) * ramp * 1.4,
          LOOP - BEAT * 2, 0.20)

    # --- fold: every tail past the end wraps to the front ------------------
    loop = out[:N] + out[N:2 * N]

    # The top octave at this sample rate carries nothing but hiss from the noise
    # sources, so it comes off: it costs no musical content and takes the fizz
    # with it.
    loop = lp1_wrap(loop, 8600.0)

    # High shelf, set by measurement rather than by ear: the mix came out at
    # 2.1% of its energy in the 1.5-4 kHz band against the menu theme's 3.5 and
    # One Hit Kill's 5.7. Additive synthesis and a sixteenth bass put almost
    # everything under 400 Hz on their own, so the top has to be put back.
    loop = loop + (loop - lp1_wrap(loop, 1500.0)) * HIGH_SHELF

    loop -= loop.mean()
    peak = float(np.max(np.abs(loop)))
    if peak > 0:
        loop *= 0.80 / peak
    loop = np.tanh(loop * 1.15) / np.tanh(1.15) * 0.80

    # A 2 ms ramp on each end. The fold wraps the TAILS round, but the sample
    # VALUES at the two ends still meet cold: the file ends on the riser and
    # begins on a kick transient, and that join measured at the 99.97th
    # percentile of the track's own sample-to-sample steps - an outlier, i.e. a
    # click. Two milliseconds is below the ear's resolution for a level change
    # and it lands on a kick attack, where nothing is sustaining across the
    # join anyway.
    ramp = int(0.002 * RATE)
    loop[:ramp] *= np.linspace(0.0, 1.0, ramp) ** 0.5
    loop[-ramp:] *= np.linspace(1.0, 0.0, ramp) ** 0.5

    pcm = (np.clip(loop, -1.0, 1.0) * 32767.0).astype("<i2")

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    targets = [os.path.join(root, "assets", "USA", "Sound", "raidbgm.wav")]
    for cfg in ("Debug", "Release"):
        d = os.path.join(root, "bin", cfg, "USA", "Sound")
        if os.path.isdir(d):
            targets.append(os.path.join(d, "raidbgm.wav"))

    for path in targets:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(pcm.tobytes())
        print("wrote %s (%d bytes, %.2f s)" % (path, os.path.getsize(path),
                                               len(pcm) / RATE))


if __name__ == "__main__":
    main()
