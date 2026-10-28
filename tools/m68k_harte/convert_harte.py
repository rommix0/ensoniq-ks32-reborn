"""Convert Tom Harte's 68000 per-instruction JSON tests to the compact .m68t format.

    convert_harte.py <in dir of *.json.gz> <out dir>     one .m68t per input, same base name
    convert_harte.py --json <file.json> <out.m68t>       one plain JSON file

.m68t, little-endian: "M68T", u32 version = 1, u32 count; each test: u16 nameLen, name bytes,
State initial, State final, u32 cycles.
State: u32 d[8], a[7], usp, ssp, pc; u16 sr; u16 prefetch[2]; u32 ramCount;
ramCount x (u32 addr, u8 value).
Run with `python -I` (standard library only).
"""
import gzip
import json
import os
import struct
import sys

REGS = ["d%d" % i for i in range(8)] + ["a%d" % i for i in range(7)] + ["usp", "ssp", "pc"]


def pack_state(s, out):
    for r in REGS:
        out += struct.pack("<I", s[r])
    out += struct.pack("<H", s["sr"])
    pf = s["prefetch"]
    if len(pf) != 2:
        raise ValueError("prefetch must have 2 entries")
    out += struct.pack("<HH", pf[0], pf[1])
    ram = s["ram"]
    out += struct.pack("<I", len(ram))
    for addr, val in ram:
        out += struct.pack("<IB", addr, val)


def convert_tests(tests):
    out = bytearray(b"M68T")
    out += struct.pack("<II", 1, len(tests))
    for t in tests:
        name = t["name"].encode("utf-8")
        out += struct.pack("<H", len(name))
        out += name
        pack_state(t["initial"], out)
        pack_state(t["final"], out)
        out += struct.pack("<I", t["length"])
    return bytes(out)


def convert_file(src, dst, opener):
    with opener(src, "rb") as f:
        tests = json.loads(f.read().decode("utf-8"))
    data = convert_tests(tests)
    with open(dst, "wb") as f:
        f.write(data)
    return len(tests)


def main(argv):
    try:
        if len(argv) == 4 and argv[1] == "--json":
            n = convert_file(argv[2], argv[3], open)
            print("%s: %d tests" % (argv[3], n))
            return 0
        if len(argv) == 3 and not argv[1].startswith("--"):
            src_dir, dst_dir = argv[1], argv[2]
            os.makedirs(dst_dir, exist_ok=True)
            names = sorted(n for n in os.listdir(src_dir) if n.endswith(".json.gz"))
            if not names:
                sys.stderr.write("convert_harte: no *.json.gz in %s\n" % src_dir)
                return 1
            total = 0
            for n in names:
                base = n[: -len(".json.gz")]
                total += convert_file(os.path.join(src_dir, n), os.path.join(dst_dir, base + ".m68t"), gzip.open)
            print("%d files, %d tests -> %s" % (len(names), total, dst_dir))
            return 0
    except (OSError, ValueError, KeyError, TypeError, struct.error) as e:
        sys.stderr.write("convert_harte: %s: %s\n" % (type(e).__name__, e))
        return 1
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
