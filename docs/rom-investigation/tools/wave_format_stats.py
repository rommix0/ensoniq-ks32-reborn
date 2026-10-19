#!/usr/bin/env python3
"""Signal statistics used to decide the KS-32 wave ROM format (open item A).

Run:  python -I wave_format_stats.py <dir containing sq1-u25.bin sq1-u26.bin rom2.u39 rom3.u38>

Pure Python, no third-party modules.  For each candidate interpretation it
prints, over all non-silent 256-sample blocks of the ROM:

  r1      lag-1 autocorrelation of the sample stream (audio near 1, noise near 0)
  dE      mean squared first difference / signal variance (audio << 1, white noise ~2)
  zcr     zero crossings per sample
  sfm     spectral flatness (geometric/arithmetic mean of the power spectrum,
          256-point Hann FFT; audio << 1, white noise ~0.56 for Hann/periodogram)
  lo_r1   lag-1 autocorrelation of the LOW byte alone (a genuine low byte of a
          16-bit sample is noise-like: near 0)

The interpretation whose sample stream looks like audio AND whose low byte
looks like quantisation residue (or is absent) wins.
"""
import cmath
import math
import os
import sys

BLOCK = 256


def fft(a):
    n = len(a)
    j = 0
    a = list(a)
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    size = 2
    while size <= n:
        w = cmath.exp(-2j * math.pi / size)
        half = size // 2
        for start in range(0, n, size):
            wk = 1
            for k in range(half):
                u = a[start + k]
                v = a[start + k + half] * wk
                a[start + k] = u + v
                a[start + k + half] = u - v
                wk *= w
        size <<= 1
    return a


HANN = [0.5 - 0.5 * math.cos(2 * math.pi * i / BLOCK) for i in range(BLOCK)]


def block_stats(x):
    n = len(x)
    m = sum(x) / n
    y = [v - m for v in x]
    var = sum(v * v for v in y) / n
    if var < 4.0:  # silent / constant fill: skip
        return None
    r1 = sum(y[i] * y[i + 1] for i in range(n - 1)) / (n - 1) / var
    de = sum((x[i + 1] - x[i]) ** 2 for i in range(n - 1)) / (n - 1) / var
    zc = sum(1 for i in range(n - 1) if (y[i] < 0) != (y[i + 1] < 0)) / (n - 1)
    spec = fft([y[i] * HANN[i] for i in range(n)])
    p = [abs(c) ** 2 + 1e-9 for c in spec[1:n // 2]]
    sfm = math.exp(sum(math.log(v) for v in p) / len(p)) / (sum(p) / len(p))
    return r1, de, zc, sfm


def summarize(name, samples, lowbytes=None, stride=16):
    """samples: list of signed ints. Analyse every `stride`-th block."""
    acc = [0.0] * 5
    cnt = 0
    nblocks = len(samples) // BLOCK
    for b in range(0, nblocks, stride):
        seg = samples[b * BLOCK:(b + 1) * BLOCK]
        s = block_stats(seg)
        if s is None:
            continue
        lo = 0.0
        if lowbytes is not None:
            lseg = lowbytes[b * BLOCK:(b + 1) * BLOCK]
            ls = block_stats(lseg)
            lo = ls[0] if ls else 0.0
        for i, v in enumerate(s + (lo,)):
            acc[i] += v
        cnt += 1
    if not cnt:
        print('%-46s  (all blocks silent)' % name)
        return
    a = [v / cnt for v in acc]
    lo = ('%6.3f' % a[4]) if lowbytes is not None else '   n/a'
    print('%-46s blocks=%5d r1=%6.3f dE=%7.3f zcr=%6.3f sfm=%6.3f lo_r1=%s'
          % (name, cnt, a[0], a[1], a[2], a[3], lo))


def s8(b):
    return b - 256 if b >= 128 else b


def s16(v):
    return v - 65536 if v >= 32768 else v


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else '.'
    rd = lambda f: open(os.path.join(d, f), 'rb').read()
    u25, u26, r2, r3 = rd('sq1-u25.bin'), rd('sq1-u26.bin'), rd('rom2.u39'), rd('rom3.u38')

    print('--- bank 0 (sq1-u25 / sq1-u26) ---')
    for nm, c in (('u25', u25), ('u26', u26)):
        summarize('%s as 8-bit samples (1 byte = 1 sample)' % nm, [s8(b) for b in c])
        summarize('%s even bytes only' % nm, [s8(b) for b in c[0::2]])
        summarize('%s odd bytes only' % nm, [s8(b) for b in c[1::2]])
        summarize('%s as 16-bit BE pairs (even=hi)' % nm,
                  [s16(c[i] << 8 | c[i + 1]) for i in range(0, len(c), 2)],
                  [c[i + 1] for i in range(0, len(c), 2)])
        summarize('%s as 16-bit LE pairs (odd=hi)' % nm,
                  [s16(c[i + 1] << 8 | c[i]) for i in range(0, len(c), 2)],
                  [c[i] for i in range(0, len(c), 2)])
    summarize('u25=hi, u26=lo, same address', [s16(a << 8 | b) for a, b in zip(u25, u26)], list(u26))
    summarize('u26=hi, u25=lo, same address', [s16(b << 8 | a) for a, b in zip(u25, u26)], list(u25))
    # cross-correlation of u25 and u26 at equal index (a lo/hi pair would correlate weakly
    # but u26 would then be white); report raw correlation of the two byte streams
    n = len(u25)
    a = [s8(v) for v in u25]
    b = [s8(v) for v in u26]
    ma, mb = sum(a) / n, sum(b) / n
    cov = sum((x - ma) * (y - mb) for x, y in zip(a, b)) / n
    va = sum((x - ma) ** 2 for x in a) / n
    vb = sum((y - mb) ** 2 for y in b) / n
    print('corr(u25[i], u26[i]) = %.4f' % (cov / math.sqrt(va * vb)))

    print('--- bank 1 (rom2.u39 / rom3.u38) ---')
    for nm, c in (('rom2', r2), ('rom3', r3)):
        summarize('%s 16-bit LE (even byte = low)' % nm,
                  [s16(c[i + 1] << 8 | c[i]) for i in range(0, len(c), 2)],
                  [c[i] for i in range(0, len(c), 2)])
        summarize('%s 16-bit BE (even byte = high)' % nm,
                  [s16(c[i] << 8 | c[i + 1]) for i in range(0, len(c), 2)],
                  [c[i + 1] for i in range(0, len(c), 2)])
        summarize('%s as 8-bit samples' % nm, [s8(v) for v in c])
    summarize('rom2=hi rom3=lo same address (byte pairs)', [s16(a << 8 | b) for a, b in zip(r2, r3)], list(r3))


if __name__ == '__main__':
    main()
