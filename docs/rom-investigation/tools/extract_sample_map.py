#!/usr/bin/env python3
"""Extract the KS-32 waveform / multisample map from OS v3.01.

Run:  python -I extract_sample_map.py <ks32v301upper.bin> <ks32v301lower.bin> <out dir>

Writes <out>/sample_map.json and <out>/sample_map.csv (one row per zone).

Firmware facts this relies on (see ../findings.md item 2a):
  * Waveform number W = sound-program byte 0x6C & 0x7F.
  * Internal set: word table at 0xC1FA8E (128 entries); first zone record of W
    is at 0xC1E636 + table[W].  (routine 0xC1ACE2 / 0xC1C3CC)
  * Expansion set (rom3.u38, used when the program's flag bytes give the 0x1515
    pattern and ROM-present flag $DBA1 is set): directory at 0xC1DDD2, a copy of
    the directory at the start of rom3: entry W at 0xC1DDD4 + 14*W =
    [word offset][12-char name]; first zone at 0xC1DDD2 + offset.  The firmware
    then sets CR.BS and bit 28 (= word address bit 19) on every address, i.e.
    rom3 sits in the upper half of bank 1.
  * Zone record, 14 bytes, big-endian:
        +0  int16  pitch word w   (FC = 0x400 * 2^((key*256 + w)/3072); root key = -w/256)
        +2  u32    start  (OTIS 29-bit address format, addr<<9).  Low byte = top key of zone.
        +6  u32    loop start.  Low byte bits 4:0 = loop-mode index (table 0xC0A83C -> CR low byte)
        +10 u32    end.  Low byte = genuine END fraction bits 8:5 (chip ignores bits 4:0)
    bit 29 of an address = bank select (BS), bits 28:9 = 20-bit word address.
  * The firmware walks zones while note > top key; the last zone of a waveform has top key 0x7F.
"""
import csv
import json
import os
import struct
import sys

LOOP_CR = 0xC0A83C          # loop-mode index -> CR low byte
WAVE_TABLE = 0xC1FA8E       # 128 word offsets
ZONE_BASE = 0xC1E636
EXP_DIR = 0xC1DDD2          # copy of the rom3 directory


def build_os(upper, lower):
    img = bytearray(0x40000)
    img[0::2] = upper
    img[1:0x20000:2] = lower
    return bytes(img)


def zone(img, a, force_rom3=False):
    o = a - 0xC00000
    w, s, l, e = struct.unpack('>hIII', img[o:o + 14])
    if force_rom3:  # firmware: bset #28 on all addresses, CR.BS = 1
        s, l, e = s | 1 << 28, l | 1 << 28, e | 1 << 28
        bank = 1
    else:
        bank = (s >> 29) & 1
    mode = l & 0x1F
    cr_low = img[LOOP_CR - 0xC00000 + mode] if mode < 8 else None
    addr = lambda v: (v >> 9) & 0xFFFFF
    return {
        'rec_addr': '%#08x' % a, 'top_key': s & 0x7F, 'pitch_word': w,
        'root_key': round(-w / 256.0, 3),
        'bank': bank, 'start': addr(s), 'loop_start': addr(l), 'end': addr(e),
        'end_frac16': (e >> 5) & 0xF,
        'loop_mode_index': mode, 'cr_low_byte': ('%#04x' % cr_low) if cr_low is not None else None,
        'looping': bool(cr_low is not None and cr_low & 0x08),
        'bidirectional': bool(cr_low is not None and cr_low & 0x10),
        'raw': '%04X %08X %08X %08X' % (w & 0xFFFF, s, l, e),
    }


def zones_from(img, a, force_rom3=False, limit=40):
    out = []
    for _ in range(limit):
        z = zone(img, a, force_rom3)
        out.append(z)
        if z['top_key'] == 0x7F:
            break
        a += 14
    return out


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    img = build_os(open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read())
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    rd16 = lambda a: struct.unpack('>H', img[a - 0xC00000:a - 0xC00000 + 2])[0]

    internal = []
    for W in range(128):
        off = rd16(WAVE_TABLE + 2 * W)
        a = ZONE_BASE + off
        entry = {'wave': W, 'offset': '%#06x' % off}
        if a >= WAVE_TABLE:
            entry['note'] = 'offset does not point into the zone table (W>=0x79 path at 0xC1A388; unresolved)'
            internal.append(entry)
            continue
        if W >= 0x79:
            entry['note'] = ('W>=0x79: rom2 piano/sine zones (bank 1); firmware path 0xC1A37A handles '
                             'these separately (layering not decoded)')
        elif W >= 0x54:
            entry['note'] = ('0x54<=W<0x6F: start used without velocity start-modulation' if W < 0x6F else
                             '0x6F<=W<0x78: SPECIAL - started on a second OTIS voice with CR=0x0B; '
                             'start fields are not plain addresses, zone walk may run into the next wave '
                             '(unverified)' if W < 0x78 else
                             'W=0x78: firmware picks start/end records by program bytes 0x70/0x72 '
                             'from the whole table (unverified)')
        entry['zones'] = zones_from(img, a)
        internal.append(entry)

    expansion = []
    for W in range(64):
        a = EXP_DIR + 2 + 14 * W
        off = rd16(a)
        name = img[a + 2 - 0xC00000:a + 14 - 0xC00000]
        if off == 0:
            break
        expansion.append({'wave': W, 'name': name.decode('ascii', 'replace').rstrip(),
                          'offset': '%#06x' % off,
                          'zones': zones_from(img, EXP_DIR + off, force_rom3=True)})

    doc = {
        'source': 'KS-32 OS v3.01 (ks32v301upper/lower), merged at 0xC00000',
        'pitch_formula': 'P = key*256 + pitch_word (key = MIDI note incl. transpose); '
                         'FC = 0x400 * 2^(P/3072), firmware clamps P to [-0x4800, 0x5400]; '
                         'FC 0x400 = 1.0 word per output frame',
        'address_format': 'start/loop_start/end are 20-bit OTIS word addresses within the bank; '
                          'bank 0 = sq1-u25 (0x00000-0x7FFFF) + sq1-u26 (0x80000-0xFFFFF), '
                          'bank 1 = rom2.u39 (0x00000-0x7FFFF) + rom3.u38 (0x80000-0xFFFFF)',
        'loop_mode_cr_table': {str(i): '%#04x' % img[LOOP_CR - 0xC00000 + i] for i in range(5)},
        'internal_waves': internal,
        'expansion_waves_rom3': expansion,
    }
    with open(os.path.join(out, 'sample_map.json'), 'w') as f:
        json.dump(doc, f, indent=1)
    with open(os.path.join(out, 'sample_map.csv'), 'w', newline='') as f:
        cw = csv.writer(f)
        cw.writerow(['set', 'wave', 'name', 'zone', 'top_key', 'root_key', 'pitch_word', 'bank',
                     'start', 'loop_start', 'end', 'end_frac16', 'loop_mode_index', 'cr_low_byte',
                     'looping', 'bidirectional', 'rec_addr', 'raw'])
        for setname, lst in (('internal', internal), ('rom3', expansion)):
            for e in lst:
                for i, z in enumerate(e.get('zones', [])):
                    cw.writerow([setname, e['wave'], e.get('name', ''), i, z['top_key'], z['root_key'],
                                 z['pitch_word'], z['bank'], '%05X' % z['start'], '%05X' % z['loop_start'],
                                 '%05X' % z['end'], z['end_frac16'], z['loop_mode_index'], z['cr_low_byte'],
                                 int(z['looping']), int(z['bidirectional']), z['rec_addr'], z['raw']])
    nz = sum(len(e.get('zones', [])) for e in internal)
    print('internal waves with zones:', sum(1 for e in internal if 'zones' in e), 'zones:', nz)
    print('rom3 waves:', len(expansion), 'zones:', sum(len(e['zones']) for e in expansion))


if __name__ == '__main__':
    main()
