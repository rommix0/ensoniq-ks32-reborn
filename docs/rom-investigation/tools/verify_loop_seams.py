#!/usr/bin/env python3
"""Check the firmware's loop points against each wave-ROM interpretation (item A).

Run:  python -I verify_loop_seams.py <rom dir> <sample_map.json>

For every forward-looping zone (CR low byte 0x0B) the seam step
|x[loop_start] - x[end]| is compared with the mean |x[n+1]-x[n]| inside the
loop.  A correct interpretation gives seam steps no bigger than ordinary steps
(ratio ~1 or below); a wrong byte order / sample width gives seams that look
like random jumps.  As a null reference the same statistic is computed with the
loop start moved by a pseudo-random offset.

It also reports how many zone addresses fall outside the address range an
interpretation can supply (16-bit pairs from u25/u26 only give 512K words).
"""
import json
import os
import random
import statistics
import sys


def s8(b):
    return b - 256 if b >= 128 else b


def s16(v):
    return v - 65536 if v >= 32768 else v


def main():
    romdir, mapfile = sys.argv[1], sys.argv[2]
    rd = lambda f: open(os.path.join(romdir, f), 'rb').read()
    u25, u26, r2, r3 = rd('sq1-u25.bin'), rd('sq1-u26.bin'), rd('rom2.u39'), rd('rom3.u38')
    b0_8 = u25 + u26                                  # 1M 8-bit samples
    b0_be = u25 + u26                                 # read as BE 16-bit pairs (512K words)
    b1 = r2 + r3
    b0_sw = u26 + u25
    b1_sw = r3 + r2

    banks = {
        ('bank0', '8-bit, 1 byte per word (chosen)'): (lambda a: s8(b0_8[a]) * 256, 1 << 20),
        ('bank0', '8-bit, but u26 below u25'): (lambda a: s8(b0_sw[a]) * 256, 1 << 20),
        ('bank0', '16-bit BE pairs in u25..u26'): (lambda a: s16(b0_be[2 * a] << 8 | b0_be[2 * a + 1]), 1 << 19),
        ('bank0', 'u25=hi u26=lo same address'): (lambda a: s16(u25[a] << 8 | u26[a]), 1 << 19),
        ('bank1', '16-bit LE (chosen)'): (lambda a: s16(b1[2 * a + 1] << 8 | b1[2 * a]), 1 << 20),
        ('bank1', '16-bit BE'): (lambda a: s16(b1[2 * a] << 8 | b1[2 * a + 1]), 1 << 20),
        ('bank1', '16-bit LE, but rom3 below rom2'): (lambda a: s16(b1_sw[2 * a + 1] << 8 | b1_sw[2 * a]), 1 << 20),
    }

    m = json.load(open(mapfile))
    zones = []
    for e in m['internal_waves']:
        if 0x6F <= e['wave'] <= 0x78:
            continue  # special encodings, not plain addresses
        zones += e.get('zones', [])
    zones += [z for e in m['expansion_waves_rom3'] for z in e['zones']]
    fwd = [z for z in zones if z['cr_low_byte'] == '0x0b' and z['end'] - z['loop_start'] > 16]

    rnd = random.Random(1)
    for (bank, name), (get, size) in banks.items():
        bz = [z for z in fwd if z['bank'] == (0 if bank == 'bank0' else 1)]
        allz = [z for z in zones if z['bank'] == (0 if bank == 'bank0' else 1)]
        oob = sum(1 for z in allz if max(z['start'], z['loop_start'], z['end']) >= size)
        ratios, nulls = [], []
        for z in bz:
            ls, en = z['loop_start'], z['end']
            if en + 1 >= size:
                continue
            steps = [abs(get(a + 1) - get(a)) for a in range(ls, min(en, ls + 4000))]
            mean = statistics.mean(steps) or 1
            ratios.append(abs(get(ls) - get(en)) / mean)
            k = ls + rnd.randrange(8, max(9, (en - ls) // 2))
            nulls.append(abs(get(k) - get(en)) / mean)
        if not ratios:
            print('%-6s %-34s no zones' % (bank, name))
            continue
        print('%-6s %-34s zones=%3d  out-of-range=%3d/%3d  seam/step median=%6.2f  p90=%6.2f   '
              'null median=%6.2f' % (bank, name, len(ratios), oob, len(allz),
                                     statistics.median(ratios), sorted(ratios)[int(.9 * len(ratios))],
                                     statistics.median(nulls)))


if __name__ == '__main__':
    main()
