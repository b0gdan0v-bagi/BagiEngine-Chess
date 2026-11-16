#!/usr/bin/env python3
"""Synthesize the sound clips of the game into assets/sounds/.

    python tools/make_sounds.py            write the clips
    python tools/make_sounds.py --check    exit 1 when a file on disk differs from what this script makes, else 0

Standard library only. The output is the same bytes on every run (a fixed seed per clip, no clock), so a clip is a
build product of this file and --check is how a test or a person sees that the two agree. Every clip is mono, 16-bit
PCM, 44100 Hz, peaks between 0.5 and 0.8 of full scale (so the master level 1 never clips) and has a short fade-in and
a fade-out, so no click at either end.

The clips (one per sound cue of the game):
  move.wav       0.12 s  a wooden knock: low-passed noise plus a short sine thump near 180 Hz
  capture.wav    0.18 s  the same knock, louder, with a sharper click on the attack and a lower thump
  castle.wav     0.30 s  two knocks, the second lower: the king and the rook
  check.wav      0.35 s  two short tones, the second higher: a rising question
  promotion.wav  0.50 s  four rising notes of a major chord, ringing
  game_end.wav   1.20 s  a slow falling triad that rings out

No ear checked them: only their numbers (length, peak, the ends) are. To replace a clip by hand, drop its entry from
CLIPS below (otherwise --check fails and the next run overwrites the file), put the new file under the same name and keep
its row in THIRD_PARTY_NOTICES.md with its author and licence.
"""

from __future__ import annotations

import argparse
import io
import math
import random
import sys
import wave
from pathlib import Path

SAMPLE_RATE = 44100
SAMPLE_WIDTH_BYTES = 2
FULL_SCALE = 32767
OUT_DIR = Path(__file__).resolve().parent.parent / "assets" / "sounds"

FADE_IN_SECONDS = 0.003
TWO_PI = 2.0 * math.pi
# The attack of a knock or a note: a click-free ramp that is still sharp
KNOCK_ATTACK_SECONDS = 0.0008
NOTE_ATTACK_SECONDS = 0.004
# A sound is not computed further than the point where its envelope is below this fraction of its start
ENVELOPE_FLOOR = math.exp(-9.0)


class Clip:
    def __init__(self, seconds: float):
        self.samples = [0.0] * round(seconds * SAMPLE_RATE)
        self.seconds = seconds


def one_pole(cutoff_hz: float) -> float:
    return 1.0 - math.exp(-TWO_PI * cutoff_hz / SAMPLE_RATE)


def knock(clip: Clip, rng: random.Random, start: float, *, freq: float, drop: float, thump: float, noise: float,
          noise_cutoff: float, click: float, click_cutoff: float, decay: float) -> None:
    """A wooden knock: a sine that falls from freq*(1+drop) to freq, low-passed noise, and a bright click on the attack.

    decay is the decay rate of the thump per second; the noise and the click decay faster."""
    first = round(start * SAMPLE_RATE)
    count = min(len(clip.samples) - first, round(-math.log(ENVELOPE_FLOOR) / decay * SAMPLE_RATE))
    body = one_pole(noise_cutoff)
    bright = one_pole(click_cutoff)
    phase = 0.0
    body_state = 0.0
    bright_state = 0.0
    for i in range(count):
        t = i / SAMPLE_RATE
        attack = min(1.0, t / KNOCK_ATTACK_SECONDS)
        white = rng.uniform(-1.0, 1.0)
        body_state += body * (white - body_state)
        bright_state += bright * (white - bright_state)
        phase += TWO_PI * freq * (1.0 + drop * math.exp(-t / 0.012)) / SAMPLE_RATE
        value = (thump * math.sin(phase) * math.exp(-decay * t)
                 + noise * body_state * math.exp(-2.0 * decay * t)
                 + click * (white - bright_state) * math.exp(-6.0 * decay * t))
        clip.samples[first + i] += value * attack


def note(clip: Clip, start: float, freq: float, *, length: float, decay: float, gain: float,
         harmonics: tuple[float, ...] = (1.0, 0.35, 0.12)) -> None:
    """A soft tone with overtones; its last 12 ms fade out, so a note that ends before the clip does does not click."""
    release = 0.012
    first = round(start * SAMPLE_RATE)
    count = min(len(clip.samples) - first, round(length * SAMPLE_RATE))
    for i in range(count):
        t = i / SAMPLE_RATE
        attack = min(1.0, t / NOTE_ATTACK_SECONDS)
        left = length - t
        tail = min(1.0, left / release)
        tone = sum(level * math.sin(TWO_PI * freq * (k + 1) * t) for k, level in enumerate(harmonics))
        clip.samples[first + i] += gain * tone * math.exp(-decay * t) * attack * tail


def finish(clip: Clip, peak: float, fade_out: float) -> bytes:
    """Fade both ends, scale to the peak (a fraction of full scale) and encode as a WAV file."""
    samples = clip.samples
    fade_in_count = round(FADE_IN_SECONDS * SAMPLE_RATE)
    fade_out_count = round(fade_out * SAMPLE_RATE)
    for i in range(min(fade_in_count, len(samples))):
        samples[i] *= i / fade_in_count
    for i in range(fade_out_count):
        samples[len(samples) - 1 - i] *= 0.5 - 0.5 * math.cos(math.pi * i / fade_out_count)
    top = max(abs(s) for s in samples)
    scale = peak * FULL_SCALE / top
    pcm = bytearray()
    for s in samples:
        pcm += int(round(s * scale)).to_bytes(SAMPLE_WIDTH_BYTES, "little", signed=True)
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(SAMPLE_WIDTH_BYTES)
        out.setframerate(SAMPLE_RATE)
        out.writeframes(bytes(pcm))
    return buffer.getvalue()


def make_move() -> bytes:
    clip = Clip(0.12)
    knock(clip, random.Random(1001), 0.0, freq=180.0, drop=0.5, thump=1.0, noise=1.4, noise_cutoff=1800.0,
          click=0.25, click_cutoff=2500.0, decay=38.0)
    return finish(clip, peak=0.55, fade_out=0.03)


def make_capture() -> bytes:
    clip = Clip(0.18)
    knock(clip, random.Random(1002), 0.0, freq=120.0, drop=0.8, thump=1.6, noise=1.8, noise_cutoff=3200.0,
          click=1.2, click_cutoff=1500.0, decay=26.0)
    return finish(clip, peak=0.78, fade_out=0.05)


def make_castle() -> bytes:
    clip = Clip(0.30)
    rng = random.Random(1003)
    knock(clip, rng, 0.0, freq=170.0, drop=0.5, thump=1.0, noise=1.2, noise_cutoff=1600.0,
          click=0.3, click_cutoff=2200.0, decay=34.0)
    knock(clip, rng, 0.15, freq=135.0, drop=0.5, thump=1.2, noise=1.2, noise_cutoff=1300.0,
          click=0.3, click_cutoff=1800.0, decay=30.0)
    return finish(clip, peak=0.65, fade_out=0.06)


def make_check() -> bytes:
    clip = Clip(0.35)
    note(clip, 0.0, 740.0, length=0.15, decay=14.0, gain=0.6)
    note(clip, 0.15, 988.0, length=0.20, decay=9.0, gain=0.8)
    return finish(clip, peak=0.6, fade_out=0.08)


def make_promotion() -> bytes:
    clip = Clip(0.50)
    for index, freq in enumerate((523.3, 659.3, 784.0, 1046.5)):
        note(clip, 0.09 * index, freq, length=0.5 - 0.09 * index, decay=7.0, gain=0.7 + 0.1 * index,
             harmonics=(1.0, 0.2, 0.05))
    return finish(clip, peak=0.65, fade_out=0.15)


def make_game_end() -> bytes:
    clip = Clip(1.20)
    for index, freq in enumerate((440.0, 349.2, 293.7, 146.8)):
        note(clip, 0.26 * index, freq, length=1.2 - 0.26 * index, decay=3.2, gain=0.8,
             harmonics=(1.0, 0.45, 0.2, 0.08))
    return finish(clip, peak=0.7, fade_out=0.35)


CLIPS = {
    "move.wav": make_move,
    "capture.wav": make_capture,
    "castle.wav": make_castle,
    "check.wav": make_check,
    "promotion.wav": make_promotion,
    "game_end.wav": make_game_end,
}


def describe(name: str, data: bytes) -> str:
    with wave.open(io.BytesIO(data), "rb") as source:
        frames = source.readframes(source.getnframes())
        count = source.getnframes()
    peak = max(abs(int.from_bytes(frames[i:i + SAMPLE_WIDTH_BYTES], "little", signed=True))
               for i in range(0, len(frames), SAMPLE_WIDTH_BYTES))
    return f"{name}: {count / SAMPLE_RATE:.3f} s, peak {peak / FULL_SCALE:.2f}, {len(data)} bytes"


def main() -> int:
    parser = argparse.ArgumentParser(description="Synthesize assets/sounds/*.wav")
    parser.add_argument("--check", action="store_true", help="exit 1 when a file on disk differs from the script's output")
    args = parser.parse_args()
    differing = 0
    for name, make in CLIPS.items():
        data = make()
        path = OUT_DIR / name
        if args.check:
            same = path.is_file() and path.read_bytes() == data
            print(f"{name}: {'same' if same else 'DIFFERS'}")
            differing += 0 if same else 1
        else:
            OUT_DIR.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print(describe(str(path.relative_to(OUT_DIR.parent.parent)).replace("\\", "/"), data))
    return 1 if differing else 0


if __name__ == "__main__":
    sys.exit(main())
