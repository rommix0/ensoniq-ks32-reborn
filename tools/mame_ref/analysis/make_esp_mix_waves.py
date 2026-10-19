# Writes the two synthetic wave images used by tests/data/esp_mix.trace.
#   python -I tools/mame_ref/analysis/make_esp_mix_waves.py tests/data
# Both are non-decreasing ramps, so every OTIS rounding step (filter, volume, output word)
# sees only non-negative operands and floor (ours) equals truncation (MAME): the OTIS outputs,
# and therefore the ESP inputs, are identical in both, and any main-output difference is the
# ESP's own.
#   esp_mix_fast.bin: min(600*i, 24000)  (rises for 40 words, then flat)
#   esp_mix_slow.bin: min(250*i, 32000)  (rises for 128 words, then flat)
import os
import struct
import sys

out_dir = sys.argv[1]
for name, slope, top in (("esp_mix_fast.bin", 600, 24000), ("esp_mix_slow.bin", 250, 32000)):
    words = [min(slope * i, top) for i in range(512)]
    with open(os.path.join(out_dir, name), "wb") as f:
        f.write(struct.pack("<512h", *words))
