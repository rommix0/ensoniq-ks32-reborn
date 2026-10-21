"""Writes the ks32play smoke-test MIDI file: tests/data/player_smoke.mid.

    python -I tools/ks32play/make_test_mid.py tests/data/player_smoke.mid

Format 1, 480 PPQ, 120 bpm (1 beat = 0.5 s), 12 beats = 6 s, one channel (0). Pure Python 3, no
third-party modules, deterministic. CC102 (effect) is not used, so the effect stays the player's
--effect. Content, in beats (0.5 s each):
  0-1.5  silence (0.75 s; the ESP boot and effect load take about 0.07 s)
  1.5-3.5  C-major arpeggio, sixteenth notes (wave 0:0, the player's default --wave)
  3.5-5.5  four-note chord, C E G B
  5.5      program change to wave 5
  5.5-9.5  sustained G4 with a pitch bend sweep up to +full, down to -full, back to centre
  9.5-11.5 the chord again on wave 5
  12       end of track
"""
import struct
import sys

PPQ = 480
CH = 0


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def track(events):
    """events: list of (tick, order, bytes). Sorted by tick then order; returns an MTrk chunk."""
    data = b""
    last = 0
    for tick, _, msg in sorted(events, key=lambda e: (e[0], e[1])):
        data += vlq(tick - last) + msg
        last = tick
    return b"MTrk" + struct.pack(">I", len(data)) + data


def beats(b):
    return int(round(b * PPQ))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_test_mid.py <out.mid>")

    tempo = [
        (0, 0, b"\xFF\x51\x03" + (500000).to_bytes(3, "big")),  # 120 bpm
        (0, 1, b"\xFF\x58\x04\x04\x02\x18\x08"),  # 4/4
        (beats(12), 9, b"\xFF\x2F\x00"),
    ]

    ev = []

    def add(tick, msg, pri=1):
        ev.append((tick, pri, msg))

    def note(start, length, key, vel=100):
        add(beats(start), bytes([0x90 | CH, key, vel]), 2)
        add(beats(start + length), bytes([0x80 | CH, key, 0]), 0)

    add(0, bytes([0xB0 | CH, 7, 127]))  # volume
    add(0, bytes([0xE0 | CH, 0x00, 0x40]))  # bend centre

    for i, key in enumerate([60, 64, 67, 72, 76, 79, 72, 67]):  # arpeggio
        note(1.5 + 0.25 * i, 0.25, key)
    for key in (60, 64, 67, 71):  # chord on the default wave
        note(3.5, 2.0, key, 90)

    add(beats(5.5), bytes([0xC0 | CH, 5]))  # program change: wave 5 of the internal set

    note(5.5, 4.0, 67)  # held G4 while the bend sweeps
    sweep = [0x2000 + int(round(0x1FFF * v)) for v in
             [i / 16 for i in range(0, 17)] + [1 - i / 16 for i in range(1, 33)] +
             [-1 + i / 16 for i in range(1, 17)]]  # 0 -> +1 -> -1 -> 0
    for i, v in enumerate(sweep):
        v = max(0, min(0x3FFF, v))
        add(beats(5.5 + 4.0 * i / len(sweep)), bytes([0xE0 | CH, v & 0x7F, v >> 7]))
    add(beats(9.5), bytes([0xE0 | CH, 0x00, 0x40]))

    for key in (60, 64, 67, 71):
        note(9.5, 2.0, key, 90)

    ev.append((beats(12), 9, b"\xFF\x2F\x00"))

    header = b"MThd" + struct.pack(">IHHH", 6, 1, 2, PPQ)
    with open(sys.argv[1], "wb") as f:
        f.write(header + track(tempo) + track(ev))


main()
