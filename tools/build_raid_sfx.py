#!/usr/bin/env python3
"""build_raid_sfx.py - the EXTRA (RAID) screen's opening sting.

Unlike the achievement cue, which is a supplied sample, this one is SYNTHESISED
here: it has to land on the animation's own beats, and the only way to keep a
sound and a frame counter in step is to generate the sound from the same
numbers. The frame marks below are the ones in src/game/TitleScreen.cpp; change
them there and re-run this.

Output: assets/USA/Sound/raid.wav, mono / 22050 Hz / 16-bit PCM - the format
every shipped RE1 sound uses, and the only one DirectSound::CreateSound hands
to XAudio2 unchanged. Copy it into bin/Debug/USA/Sound and bin/Release/USA/Sound
too: the standing three-trees rule for binary assets (this script does it).

Run: python3 tools/build_raid_sfx.py
"""

import os
import struct
import wave

import numpy as np

RATE = 22050
FPS = 30.0

# The animation's marks, in frames (TitleScreen.cpp).
F_LINE, F_WORD, F_FLARE, F_PROMPT = 48, 60, 78, 90
LENGTH = 6.0


def t(n=None):
    return np.arange(int(RATE * LENGTH)) / RATE


def env(time, attack, decay, power=2.0):
    """Percussive envelope: linear attack, exponential-ish decay."""
    e = np.zeros_like(time)
    a = time < attack
    e[a] = time[a] / max(attack, 1e-6)
    d = time >= attack
    k = (time[d] - attack) / max(decay, 1e-6)
    e[d] = np.clip(1.0 - k, 0.0, 1.0) ** power
    return e


def at(sec):
    return int(sec * RATE)


def add(buf, sig, sec):
    i = at(sec)
    n = min(len(sig), len(buf) - i)
    if n > 0:
        buf[i:i + n] += sig[:n]


def main():
    time = t()
    out = np.zeros_like(time)
    rng = np.random.default_rng(0x5241)

    hit = F_WORD / FPS            # RAID lands
    line = F_LINE / FPS           # the rule opens

    # --- the bed: a low drone that is there from the first frame ------------
    d = time
    drone = (np.sin(2 * np.pi * 55.0 * d) * 0.5 +
             np.sin(2 * np.pi * 55.0 * 1.003 * d) * 0.5 +   # detune: slow beat
             np.sin(2 * np.pi * 82.5 * d) * 0.22)           # a fifth above
    drone *= np.clip(d / 0.6, 0, 1) * np.exp(-d * 0.24)
    drone *= 1.0 + 0.12 * np.sin(2 * np.pi * 0.7 * d)       # breathing
    out += drone * 0.34

    # --- the riser: noise climbing to the hit -------------------------------
    n = at(hit)
    r = rng.standard_normal(n)
    # one-pole low-pass whose cutoff opens as it rises - a filter sweep done
    # the cheap way, by mixing the filtered signal back towards the raw one
    lp = np.zeros(n)
    acc = 0.0
    for i in range(n):
        acc += (r[i] - acc) * (0.002 + 0.06 * (i / n) ** 2)
        lp[i] = acc
    ramp = (np.arange(n) / n) ** 3
    # Duck the riser out over the last 80 ms. Without this it is still at full
    # level when the hit lands and the hit has nothing to punch through - the
    # gap before the impact is what makes the impact.
    duck = np.ones(n)
    k = at(0.08)
    if k < n:
        duck[-k:] = np.linspace(1.0, 0.0, k) ** 2
    add(out, lp * ramp * duck * 1.8, 0.0)

    # a thin metallic tone sliding up under it
    f = 180.0 * (1.0 + 2.2 * (np.arange(n) / n) ** 2)
    add(out, np.sin(2 * np.pi * np.cumsum(f) / RATE) * ramp * duck * 0.16, 0.0)

    # --- the rule opening: a short air-tick ---------------------------------
    n2 = at(0.35)
    tick = rng.standard_normal(n2) * env(np.arange(n2) / RATE, 0.004, 0.30, 3.0)
    add(out, tick * 0.11, line)

    # --- the hit ------------------------------------------------------------
    n3 = len(time) - at(hit)
    h = np.arange(n3) / RATE

    # sub: a pitch drop, which is what gives it weight on small speakers too
    sub_f = 92.0 * np.exp(-h * 5.0) + 42.0
    sub = np.sin(2 * np.pi * np.cumsum(sub_f) / RATE) * env(h, 0.006, 1.5, 1.6)

    # body: a struck-metal cluster, inharmonic on purpose
    body = np.zeros(n3)
    for f0, a0, dk in ((146.0, 1.00, 1.1), (233.0, 0.52, 0.8),
                       (349.0, 0.30, 0.55), (523.0, 0.16, 0.35),
                       (784.0, 0.09, 0.22)):
        body += np.sin(2 * np.pi * f0 * h) * a0 * env(h, 0.003, dk, 2.2)

    # transient: the crack that makes it read as an impact
    crack = rng.standard_normal(n3) * env(h, 0.001, 0.12, 4.0)

    strike = sub * 1.05 + body * 0.34 + crack * 0.40

    # --- room ---------------------------------------------------------------
    # Four taps off the STRIKE only, not off the whole buffer. Feeding the mix
    # back into itself smears the riser across the impact and the two stop
    # being separate events; the strike is the only thing that should ring.
    wet = np.zeros(n3)
    for delay, gain in ((0.071, 0.30), (0.113, 0.22), (0.173, 0.15),
                        (0.229, 0.10)):
        d0 = at(delay)
        if d0 < n3:
            wet[d0:] += strike[:n3 - d0] * gain

    # and the air it leaves behind
    airn = min(n3, at(1.8))
    air = np.zeros(n3)
    ah = np.arange(airn) / RATE
    air[:airn] = rng.standard_normal(airn) * np.exp(-ah * 2.2) * 0.05

    add(out, strike + wet + air, hit)

    # the low octave of the drone, restruck by the hit - this is what carries
    # the screen while the prompt is still coming up
    tailn = n3
    th = np.arange(tailn) / RATE
    tail = (np.sin(2 * np.pi * 55.0 * th) * 0.6 +
            np.sin(2 * np.pi * 110.0 * th) * 0.28)
    add(out, tail * np.exp(-th * 0.9) * 0.30, hit)

    # --- level --------------------------------------------------------------
    out -= out.mean()
    peak = float(np.max(np.abs(out)))
    if peak > 0:
        out *= 0.89 / peak
    # soft clip, so the hit stays hot without a hard edge on it
    out = np.tanh(out * 1.25) / np.tanh(1.25) * 0.89

    fade = at(0.25)
    out[-fade:] *= np.linspace(1.0, 0.0, fade)

    pcm = (np.clip(out, -1.0, 1.0) * 32767.0).astype("<i2")

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    targets = [os.path.join(root, "assets", "USA", "Sound", "raid.wav")]
    for cfg in ("Debug", "Release"):
        d = os.path.join(root, "bin", cfg, "USA", "Sound")
        if os.path.isdir(d):
            targets.append(os.path.join(d, "raid.wav"))

    for path in targets:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(pcm.tobytes())
        print("wrote %s (%d bytes)" % (path, os.path.getsize(path)))


if __name__ == "__main__":
    main()
