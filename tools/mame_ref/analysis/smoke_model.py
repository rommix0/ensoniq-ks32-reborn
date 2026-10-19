# Attribution model for the smoke-trace differences between ens_trace and mame_ref
# (docs/mame-ref.md). It re-implements the OTIS path of tests/data/smoke.trace only (two
# looped voices, 4 LP poles with K = 4095, the volumes of that trace) with each rounding choice
# switchable, checks that "floor everywhere" reproduces ours.pcm and "MAME's truncations"
# reproduces mame.pcm exactly, then switches one knob at a time to split the differences.
# Main L/R are compared with the model's OTIS ch1 because the smoke ESP program copies SER0
# to SER3 unchanged in both implementations.
#
#   python -I tools/mame_ref/analysis/smoke_model.py tests/data/smoke_wave.bin ours.pcm mame.pcm
import struct, sys
wave_path, ours_path, mame_path = sys.argv[1:4]
w = struct.unpack('<%dh' % (len(open(wave_path,'rb').read())//2), open(wave_path,'rb').read())
def rd(p):
    b=open(p,'rb').read(); return struct.unpack('<%dh'%(len(b)//2), b)
OURS, MAME = rd(ours_path), rd(mame_path)
def tdiv(a, b):  # C truncating division
    q = abs(a)//abs(b); return q if (a>=0)==(b>0) else -q
def word(i): return w[i] if 0 <= i < len(w) else 0
K = 0xFFF
# voices: (step in 1/512, end int, lvol e/m, rvol e/m, channel)
voices = [dict(step=512, end=64, l=(15,0), r=(14,0), ch=0), dict(step=768, end=64, l=(15,0), r=(13,0), ch=1)]
def run(filt_trunc, out_mame, strict_end):
    st = [dict(acc=0, p=[0,0,0,0]) for _ in voices]
    frames=[]
    for f in range(64):
        acc20 = {0:[0,0],1:[0,0]}
        for v, s in zip(voices, st):
            a = s['acc']; i, fr = a >> 9, a & 511
            s1, s2 = word(i), word(i+1)
            x = s1 + (((s2-s1)*fr) >> 9)
            for k in range(4):
                y1 = s['p'][k]
                y = y1 + (tdiv(K*(x-y1), 4096) if filt_trunc else (K*(x-y1)) >> 12)
                if not filt_trunc: y = max(-32768, min(32767, y))
                s['p'][k] = y; x = y
            for side,(e,m) in enumerate((v['l'], v['r'])):
                if out_mame:
                    lookup = ((16+m) << 11) >> (16-e)
                    acc20[v['ch']][side] += (x*lookup) >> 11
                else:
                    p = (x*(16+m)) >> 5
                    acc20[v['ch']][side] += (p >> (15-e)) if e else 0
            n = a + v['step']; end = v['end'] << 9
            if (n > end) if strict_end else (n >= end): n = n - end
            s['acc'] = n
        def conv(val):
            if out_mame:
                val = max(-(1<<19), min((1<<19)-1, val)); return tdiv(val, 16)
            return val  # small signal: no O6 saturation
        frames += [conv(acc20[0][0]), conv(acc20[0][1]), conv(acc20[1][0]), conv(acc20[1][1])]
    return frames
def cmp(model, ref):  # compare model (aux L/R + ch1 L/R) with ref (aux, main = ch1 via ESP copy)
    return sum(1 for a,b in zip(model, ref) if a!=b)
ours_m = run(False, False, False); mame_m = run(True, True, True)
print('model(floor filter, ours volume/output, >=END) vs ours mismatches:', cmp(ours_m, OURS))
print('model(trunc filter, MAME volume/trunc out, >END) vs MAME mismatches:', cmp(mame_m, MAME))
# Attribution: change one knob at a time from ours.
only_filt = run(True, False, False); only_out = run(False, True, False)
tot = sum(1 for a,b in zip(OURS, MAME) if a!=b)
print('samples differing ours vs MAME:', tot)
fil = sum(1 for a,b in zip(OURS, only_filt) if a!=b)
out = sum(1 for a,b in zip(OURS, only_out) if a!=b)
print('ours -> only filter trunc changes', fil, 'samples; only output/volume path changes', out)
for ch in range(4):
    df = [b-a for a,b in zip(OURS[ch::4], only_filt[ch::4])]
    do = [b-a for a,b in zip(OURS[ch::4], only_out[ch::4])]
    dt = [b-a for a,b in zip(OURS[ch::4], MAME[ch::4])]
    print('ch%d filter-only: n=%d max=%d | output-only: n=%d max=%d | total: n=%d max=%d' % (ch,
        sum(1 for d in df if d), max(map(abs,df)), sum(1 for d in do if d), max(map(abs,do)),
        sum(1 for d in dt if d), max(map(abs,dt))))
