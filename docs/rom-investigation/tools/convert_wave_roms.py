#!/usr/bin/env python3
"""Convert the KS-32 wave ROMs into the two OTIS sound-memory banks.

Run:  python -I convert_wave_roms.py <rom dir> <out dir>

Writes <out dir>/bank0.bin and <out dir>/bank1.bin: each 1M (1048576) words of
little-endian uint16, indexed by the OTIS 20-bit word address A19..A0.

Interpretation (see ../findings.md, item A):
  bank 0 (BS=0): sq1-u25.bin = words 0x00000-0x7FFFF, sq1-u26.bin = words
                 0x80000-0xFFFFF.  Each ROM byte is one signed 8-bit sample on
                 D15..D8; the low byte D7..D0 is 0 (no nibble ROM on the KS-32).
  bank 1 (BS=1): rom2.u39 = words 0x00000-0x7FFFF, rom3.u38 = words
                 0x80000-0xFFFFF.  Each ROM is already a stream of 16-bit
                 little-endian words (file byte 2n = D7..D0, 2n+1 = D15..D8).
"""
import os
import sys

WORDS = 1 << 20
HALF = 1 << 19


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    rd = lambda f: open(os.path.join(src, f), 'rb').read()
    u25, u26, r2, r3 = rd('sq1-u25.bin'), rd('sq1-u26.bin'), rd('rom2.u39'), rd('rom3.u38')
    assert len(u25) == len(u26) == HALF and len(r2) == len(r3) == 2 * HALF

    bank0 = bytearray(2 * WORDS)
    bank0[1:2 * HALF:2] = u25           # high byte of words 0x00000-0x7FFFF
    bank0[2 * HALF + 1::2] = u26        # high byte of words 0x80000-0xFFFFF
    bank1 = r2 + r3

    os.makedirs(dst, exist_ok=True)
    for name, data in (('bank0.bin', bank0), ('bank1.bin', bank1)):
        assert len(data) == 2 * WORDS
        with open(os.path.join(dst, name), 'wb') as f:
            f.write(data)
        print('wrote', os.path.join(dst, name), len(data), 'bytes')


if __name__ == '__main__':
    main()
