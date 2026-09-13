#!/usr/bin/env python3
"""build_achievement_sfx.py - prepare the achievement toast's cue.

The toast plays ONE sound, `Sound/achv.wav`, and only on an unlock (a progress
step is silent by design - see src/game/Achievements.cpp). The cue is a
supplied sample rather than synthesis, so this script converts whatever file
you hand it into the shape the engine's own WAV loader expects and drops it in
the data tree:

  * mono, 22050 Hz, 16-bit PCM - the format every shipped RE1 sound uses.
    DirectSound::CreateSound walks the RIFF chunks and hands the PCM straight
    to XAudio2, so anything else plays at the wrong speed or not at all.
  * silence trimmed from both ends (below -50 dBFS), keeping 5 ms of pre-roll
    and 10 ms of tail so the attack is not clipped.
  * peak-normalised to -1 dBFS. The runtime attenuates from there, riding the
    options screen's SFX volume, so the file itself should be hot.

Usage, from the repo root:

  python3 tools/build_achievement_sfx.py <input audio> [output.wav]

`input audio` may be anything ffmpeg reads (mp3, ogg, flac, wav...); a WAV
that is already mono/22050/16-bit is handled without ffmpeg at all. The
default output is assets/USA/Sound/achv.wav - remember the shipped asset has
to be copied into bin/Debug/USA/Sound and bin/Release/USA/Sound as well, the
standing three-trees rule for every binary asset in this project.

Nothing is generated: run this only when changing the sound.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

RATE = 22050
TRIM_FLOOR = 0.003        # ~ -50 dBFS
PRE_ROLL = 110            # samples kept before the first audible one (5 ms)
TAIL = 220                # samples kept after the last (10 ms)
PEAK = 0.89               # -1 dBFS


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def to_raw_wav(src, dst):
    """Get the input into mono/22050/16-bit, via ffmpeg unless it already is."""
    if src.lower().endswith(".wav"):
        with wave.open(src, "rb") as w:
            if (w.getnchannels(), w.getframerate(), w.getsampwidth()) == (1, RATE, 2):
                shutil.copyfile(src, dst)
                return
    if shutil.which("ffmpeg") is None:
        raise SystemExit("ffmpeg not found, and the input is not already "
                         "mono %d Hz 16-bit WAV" % RATE)
    subprocess.check_call([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", src,
        "-ac", "1", "-ar", str(RATE), "-c:a", "pcm_s16le", dst,
    ])


def trim_and_normalize(samples):
    floor = int(32768 * TRIM_FLOOR)
    first = 0
    while first < len(samples) and abs(samples[first]) < floor:
        first += 1
    last = len(samples)
    while last > first and abs(samples[last - 1]) < floor:
        last -= 1
    if first >= last:
        raise SystemExit("input is silent")

    cut = samples[max(0, first - PRE_ROLL):last + TAIL]
    peak = max(abs(v) for v in cut) or 1
    k = (32767.0 * PEAK) / peak
    return [max(-32768, min(32767, int(v * k))) for v in cut]


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    src = sys.argv[1]
    dst = (sys.argv[2] if len(sys.argv) > 2
           else os.path.join(repo_root(), "assets", "USA", "Sound", "achv.wav"))

    tmp = os.path.join(tempfile.gettempdir(), "achv_raw.wav")
    to_raw_wav(src, tmp)

    with wave.open(tmp, "rb") as w:
        n = w.getnframes()
        samples = list(struct.unpack("<%dh" % n, w.readframes(n)))
    out = trim_and_normalize(samples)

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with wave.open(dst, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack("<%dh" % len(out), *out))

    print("wrote %s (%.3f s, %d samples)" % (dst, len(out) / float(RATE), len(out)))
    print("copy it into bin/Debug/USA/Sound and bin/Release/USA/Sound too")


if __name__ == "__main__":
    main()
