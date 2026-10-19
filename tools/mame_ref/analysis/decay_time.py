# -60 dBFS decay time of one channel of an ens_trace / mame_ref PCM file.
#   python -I tools/mame_ref/analysis/decay_time.py <file.pcm> [channel] [rate]
# channel: 0 auxL, 1 auxR, 2 mainL (default), 3 mainR. rate: frames per second (default
# 29761.9 = 30.47618 MHz / 2 / 512, the KS-32 frame rate). Same measure as tests/test_esp_programs.cpp: the end of the
# last 0.1 s window (round(0.1 * rate) frames, counted from frame 0) whose RMS is at or above
# -60 dBFS (32768 * 1e-3). Also prints the peak, the RMS of the last window and the time of the
# last nonzero sample.
import array
import math
import sys

path = sys.argv[1]
ch = int(sys.argv[2]) if len(sys.argv) > 2 else 2
rate = float(sys.argv[3]) if len(sys.argv) > 3 else 29761.9

pcm = array.array("h")
with open(path, "rb") as f:
    pcm.frombytes(f.read())
if sys.byteorder != "little":
    pcm.byteswap()
s = pcm[ch::4]
win = int(round(0.1 * rate))
thr = 32768.0 * 1e-3
last = -1
nwin = len(s) // win
rms_last = 0.0
for i in range(nwin):
    e = sum(v * v for v in s[i * win:(i + 1) * win])
    r = math.sqrt(e / win)
    if r >= thr:
        last = i
    rms_last = r
nz = max((n for n in range(len(s)) if s[n] != 0), default=-1)
peak = max((abs(v) for v in s), default=0)
db = 20 * math.log10(rms_last / 32768.0) if rms_last > 0 else float("-inf")
print("%s ch%d: frames %d (%.1f s), peak %d, -60 dBFS at %.1f s%s, last window rms %.3f (%.1f dBFS),"
      " last nonzero sample at %.2f s"
      % (path, ch, len(s), len(s) / rate, peak, (last + 1) * 0.1,
         " (never below within the file)" if last == nwin - 1 else "", rms_last, db,
         (nz + 1) / rate))
