#!/usr/bin/env python3
"""Extract the ESP (ES5510) effect programs from the KS-32 OS ROM.

Run:  python -I extract_esp_programs.py <ks32v301upper.bin> <ks32v301lower.bin> <out dir>

What the firmware does (OS v3.01, see ../findings.md item 2b):
  * 0xC1FC5A: table of 11 longword pointers, one per effect program index
    (D0 = index; routine 0xC1C8C0).  A pointer P is a "byte-area" address:
    the program's real ROM address is 0xC20000 + 2*(P-0xC20000), i.e. the
    data are the even (upper-EPROM) bytes of 0xC20000-0xC3FFFF, which is
    upper-EPROM file offset 0x10000 + (P-0xC20000).
  * A program = 8 header bytes, then records until a type byte 0xFF:
        type(1) count(1) addr(1) then count entries
        type 1: 9 bytes/entry  = GPR[23:0] (3) + INSTR[47:0] (6)  -> host regs $00-$08, select $E0
        type 2: 6 bytes/entry  = INSTR[47:0]                      -> host regs $03-$08, select $C0
        type 3: 3 bytes/entry  = GPR[23:0], consecutive addresses -> host regs $00-$02, select $A0
        type 4: 4 bytes/entry  = addr(1) + GPR[23:0], sparse      -> host regs $00-$02, select $A0
    addr increments by one per entry (type 4 reloads it from each entry).
  * Index 10 is followed in the pointer table by (pointer, DRAM word address)
    pairs, used once per type-4 write to ABASE ($F6) / BBASE ($F7): each points
    at a table whose first word is a byte length, followed by 16-bit words the
    host writes to consecutive DRAM addresses (routine 0xC1C966).

Writes <out>/esp_programs.json and one <out>/esp_prog_NN.txt disassembly per
distinct program.
"""
import json
import os
import struct
import sys

BYTE_AREA = 0xC20000
PTR_TABLE = 0xC1FC5A
N_PROGRAMS = 11

ALU = ['ADD', 'SUB', 'ADDU', 'SUBU', 'CMP', 'AND', 'OR', 'XOR',
       'ABS', 'MOV', 'ASL2', 'ASL8', 'LS15', 'DIFF', 'ASR', 'END']
RAM = ['RD_DELAY', 'WR_DELAY', 'RD_TABA', 'WR_TABA', 'RD_TABB', 'RD_DELAY_DUMP', 'IO_RD?', 'IO_WR?']
SPR = {0xEA: 'SER0R', 0xEB: 'SER0L', 0xEC: 'SER1R', 0xED: 'SER1L', 0xEE: 'SER2R', 0xEF: 'SER2L',
       0xF0: 'SER3R', 0xF1: 'SER3L', 0xF2: 'MACL', 0xF3: 'MACH', 0xF4: 'DIL/MEMSIZ', 0xF5: 'DLENGTH',
       0xF6: 'ABASE', 0xF7: 'BBASE', 0xF8: 'DBASE', 0xF9: 'SIGREG', 0xFA: 'CCR', 0xFB: 'CMR',
       0xFC: 'MINUS1', 0xFD: 'MIN', 0xFE: 'MAX', 0xFF: 'ZERO'}


def reg(n):
    return SPR.get(n, 'R%02X' % n)


def build_os(upper, lower):
    os_img = bytearray(0x40000)
    os_img[0::2] = upper
    os_img[1:0x20000:2] = lower
    return bytes(os_img)


def decode_program(area, off):
    """area = upper-EPROM bytes 0x10000-0x1FFFF; off = logical offset."""
    p = off
    header = area[p:p + 8]
    p += 8
    gpr, instr, recs = {}, {}, []
    while True:
        t = area[p]
        p += 1
        if t == 0xFF:
            break
        if t not in (1, 2, 3, 4):
            raise ValueError('bad record type %#x at %#x' % (t, p - 1))
        count, addr = area[p], area[p + 1]
        p += 2
        rec = {'type': t, 'count': count, 'addr': addr, 'entries': []}
        for _ in range(count):
            if t == 4:
                addr = area[p]
                p += 1
            n = {1: 9, 2: 6, 3: 3, 4: 3}[t]
            data = area[p:p + n]
            p += n
            if t in (1, 3, 4):
                gpr[addr] = int.from_bytes(data[0:3], 'big')
            if t in (1, 2):
                instr[addr] = int.from_bytes(data[-6:], 'big')
            rec['entries'].append([addr, data.hex()])
            addr = (addr + 1) & 0xFF
        recs.append(rec)
    return header, gpr, instr, recs, p - off


def disasm(word):
    d, c, b, a = (word >> 40) & 0xFF, (word >> 32) & 0xFF, (word >> 24) & 0xFF, (word >> 16) & 0xFF
    op, sel = (word >> 12) & 0xF, (word >> 8) & 0xF
    skip, mac, ram = (word >> 7) & 1, (word >> 6) & 1, (word >> 3) & 7
    s = '%-4s A=%-10s B=%-10s sel=%X | MAC%s C=%-10s D=%-10s | %s' % (
        ALU[op], reg(a), reg(b), sel, '+' if mac else ' ', reg(c), reg(d), RAM[ram])
    if skip:
        s += ' SKIP'
    return s


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    upper = open(sys.argv[1], 'rb').read()
    lower = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    img = build_os(upper, lower)
    area = upper[0x10000:]
    rd32 = lambda a: struct.unpack('>I', img[a - 0xC00000:a - 0xC00000 + 4])[0]
    ptrs = [rd32(PTR_TABLE + 4 * i) for i in range(N_PROGRAMS)]

    programs, seen = [], {}
    for idx, ptr in enumerate(ptrs):
        off = ptr - BYTE_AREA
        header, gpr, instr, recs, size = decode_program(area, off)
        end_pc = min([a for a, w in instr.items() if (w >> 12) & 0xF == 0xF] or [None]) \
            if instr else None
        prog = {
            'index': idx, 'pointer': '%#08x' % ptr, 'byte_area_offset': '%#06x' % off,
            'upper_eprom_file_offset': '%#07x' % (0x10000 + off),
            'header_hex': header.hex(), 'encoded_size_bytes': size,
            'first_END_pc': end_pc,
            'instr': {'%02X' % a: '%012X' % w for a, w in sorted(instr.items())},
            'gpr': {'%02X' % a: '%06X' % v for a, v in sorted(gpr.items())},
            'records': [{k: v for k, v in r.items() if k != 'entries'} for r in recs],
        }
        programs.append(prog)
        if off in seen:
            prog['same_as_index'] = seen[off]
            continue
        seen[off] = idx
        with open(os.path.join(out, 'esp_prog_%02d.txt' % idx), 'w') as f:
            f.write('; KS-32 OS v3.01 ESP program index %d, ptr %#x, header %s\n' % (idx, ptr, header.hex()))
            f.write('; first END at pc %s; %d instr words, %d GPR values loaded\n'
                    % (end_pc, len(instr), len(gpr)))
            f.write('; record list: %s\n' % ', '.join(
                't%d n=%d @%02X' % (r['type'], r['count'], r['addr']) for r in recs))
            f.write(';\n; pc  instr          | GPR[pc]  | decode\n')
            for a in range(0, 0xA0):
                if a not in instr:
                    continue
                g = ('%06X' % gpr[a]) if a in gpr else '------'
                f.write('%02X  %012X | %s | %s\n' % (a, instr[a], g, disasm(instr[a])))
            f.write(';\n; GPRs outside 00-9F and SPR writes:\n')
            for a in sorted(gpr):
                if a >= 0xA0:
                    f.write('%-10s = %06X\n' % (reg(a), gpr[a]))

    # DRAM preload tables listed after the pointer table (pairs: ptr, DRAM word address)
    tables = []
    p = PTR_TABLE + 4 * N_PROGRAMS
    for _ in range(2):
        tptr = rd32(p)
        dadr = struct.unpack('>H', img[p + 4 - 0xC00000:p + 6 - 0xC00000])[0]
        o = tptr - 0xC00000
        nbytes = struct.unpack('>H', img[o:o + 2])[0]
        words = [struct.unpack('>H', img[o + 2 + 2 * i:o + 4 + 2 * i])[0] for i in range(nbytes // 2)]  # loop D0=0..len-2 step 2 (0xC1C992)
        tables.append({'rom_address': '%#08x' % tptr, 'dram_word_address': '%#06x' % dadr,
                       'length_field_bytes': nbytes, 'words_written': len(words),
                       'words': ['%04X' % w for w in words]})
        p += 6
    with open(os.path.join(out, 'esp_programs.json'), 'w') as f:
        json.dump({'source': 'KS-32 OS v3.01', 'pointer_table': '%#08x' % PTR_TABLE,
                   'programs': programs, 'dram_tables_for_index_10': tables}, f, indent=1)
    for pr in programs:
        print(pr['index'], pr['pointer'], 'size', pr['encoded_size_bytes'], 'END@', pr['first_END_pc'],
              'ninstr', len(pr['instr']), 'ngpr', len(pr['gpr']), 'hdr', pr['header_hex'],
              ('same as %d' % pr['same_as_index']) if 'same_as_index' in pr else '')


if __name__ == '__main__':
    main()
