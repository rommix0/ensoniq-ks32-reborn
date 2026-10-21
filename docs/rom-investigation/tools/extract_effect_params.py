#!/usr/bin/env python3
"""Effect mix levels, output fade and FC table survey of the KS-32 OS (v3.01).

Run:  python -I extract_effect_params.py [rom_dir] [--out FILE]

rom_dir (default "ks-32") holds ks32v301upper.bin (even bytes) and
ks32v301lower.bin (odd bytes), either directly or in a "ks32" sub-directory.
They are merged at CPU 0xC00000.  The script writes
docs/rom-investigation/data/esp/effect_defaults.json (or --out) and prints the
FC-table check and the level tables on stdout.  See ../effect-params.md.

What the firmware does when a sound selects an effect (0xC0F9C2..0xC0FA32):
  * effect number e = byte 0x14 & 0x1F of the effect parameter block at RAM
    0xFFCBEE (copied from sound bytes 0x182..0x196 by 0xC0D682); e >= 13 -> 0.
  * 16-byte effect record at 0xC1FB8A + 16e: +0 word = init routine (0xC1xxxx),
    +2 word = modulation routine, +4 = parameter page set, +5 = ESP program
    index (0xC1C8C0 loads it), +6..+15 = default values of the parameter
    bytes 0x00, 0x02 .. 0x12 of the block (copied when the effect changes).
  * the init routine runs (A3 = 0xFFCBEE), then 0xC0FA70 calls the handler of
    every parameter of the effect's pages (descriptor +8), then 0xC1CB76 sets
    the mix GPRs, releases ESPHALT and ramps the output level GPR 0x7F up.
This script re-runs exactly those firmware routines in a small 68000
interpreter (only the instructions they use).  The ESP host write routine
0xC1CC80 (GPR D1.b <- D2[23:0]) and the ESPHALT release 0xC1CD3C are
intercepted, so the output is the list of GPR writes the firmware makes.

Inputs that are not in the ROM (hand-set, documented in effect-params.md):
  * the parameter block = the effect's own defaults (record bytes +6..+15);
  * master-volume slider word $DD72 = 127 (slider at maximum);
  * all other RAM = 0 (modulation sources at rest).
"""
import argparse
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_esp_programs import decode_program  # noqa: E402  (same directory)

ROM_BASE = 0xC00000
FC_TABLE = 0xC09F64          # 768 pitch words, 0x400 .. 0x7FE
MIX_TABLE = 0xC0A72C         # 128 words, quarter sine
VOL_TABLE = 0xC0A62C         # 128 words, master volume curve
FX_RECORDS = 0xC1FB8A        # 13 x 16 bytes
N_EFFECTS = 13
PROG_PTRS = 0xC1FC5A         # 11 ESP program pointers (extract_esp_programs.py)
N_PROGRAMS = 11
FX_BLOCK = 0xFFCBEE          # effect parameter block (A3 for the handlers)
PAGESET_RAM = 0xFFD476       # page-set table (RAM copy of ROM 0xC1D872..)
RAMCOPY_SRC, RAMCOPY_DST = 0xC1D872, 0xFFD1C6   # 0xC11938 copies 0x560 bytes
MASTER_VOL = 0xFFDD72        # master volume slider word (0..127)
HOST_WRITE = 0xC1CC80        # ESP GPR write with read-back verify
HALT_RELEASE = 0xC1CD3C      # DUART OP6 set (ESPHALT released)
MIX_ROUTINE = 0xC1CB76
MASTER_VOL_INDEX = 127
RAMP_7F_STEP = 0x12F         # 0xC1CC20 / 0xC1CC6A
TRANSFERS_PER_WRITE = 3      # $A0 write, $80 read-back, $C0 park (0xC1CC80)
FRAMES_PER_TRANSFER = 2      # see effect-params.md "Fade timing"


def load_rom(rom_dir):
    for d in (rom_dir, os.path.join(rom_dir, 'ks32')):
        up, lo = os.path.join(d, 'ks32v301upper.bin'), os.path.join(d, 'ks32v301lower.bin')
        if os.path.exists(up) and os.path.exists(lo):
            upper, lower = open(up, 'rb').read(), open(lo, 'rb').read()
            img = bytearray(0x40000)
            img[0::2] = upper
            img[1:0x20000:2] = lower
            return bytes(img)
    sys.exit('ks32v301upper.bin / ks32v301lower.bin not found in %r (or its ks32/)' % rom_dir)


class Rom:
    def __init__(self, img):
        self.img = img

    def b(self, a):
        return self.img[a - ROM_BASE]

    def w(self, a):
        return struct.unpack('>H', self.img[a - ROM_BASE:a - ROM_BASE + 2])[0]

    def l(self, a):
        return struct.unpack('>I', self.img[a - ROM_BASE:a - ROM_BASE + 4])[0]


def rom_ptr(word):
    """0xC117FA: a 16-bit table pointer >= 0x50E4 lives in 0xC0xxxx, else 0xC1xxxx."""
    return (0xC00000 if word >= 0x50E4 else 0xC10000) | word


# --------------------------------------------------------------------------
# Minimal 68000 interpreter (user mode, the instructions the effect routines use)

class Stop(Exception):
    pass


MASK = {1: 0xFF, 2: 0xFFFF, 4: 0xFFFFFFFF}
SIGN = {1: 0x80, 2: 0x8000, 4: 0x80000000}


def sx(v, size):
    v &= MASK[size]
    return v - (MASK[size] + 1) if v & SIGN[size] else v


class Cpu:
    def __init__(self, rom):
        self.rom = rom
        self.ram = bytearray(0x10000)
        self.d = [0] * 8
        self.a = [0] * 8
        self.pc = 0
        self.x = self.n = self.z = self.v = self.c = 0
        self.hooks = {}
        self.steps = 0
        self.written = set()      # RAM offsets written by executed code

    # memory ------------------------------------------------------------
    def _ram_index(self, a):
        a &= 0xFFFFFF
        if a >= 0xFF0000 or a < 0x10000:
            return a & 0xFFFF
        return None

    def rb(self, a):
        a &= 0xFFFFFF
        i = self._ram_index(a)
        if i is not None:
            return self.ram[i]
        if ROM_BASE <= a < ROM_BASE + 0x40000:
            return self.rom.img[a - ROM_BASE]
        raise Stop('read from unmapped/hardware address %06X at pc %06X' % (a, self.pc))

    def wb(self, a, v):
        i = self._ram_index(a)
        if i is None:
            raise Stop('write to non-RAM address %06X at pc %06X' % (a & 0xFFFFFF, self.pc))
        self.ram[i] = v & 0xFF
        self.written.add(i)

    def read(self, a, size):
        v = 0
        for k in range(size):
            v = (v << 8) | self.rb(a + k)
        return v

    def write(self, a, v, size):
        for k in range(size):
            self.wb(a + k, v >> (8 * (size - 1 - k)))

    def fetch16(self):
        v = self.read(self.pc, 2)
        self.pc = (self.pc + 2) & 0xFFFFFF
        return v

    # flags -------------------------------------------------------------
    def set_nz(self, v, size):
        v &= MASK[size]
        self.n = 1 if v & SIGN[size] else 0
        self.z = 1 if v == 0 else 0

    def logic_flags(self, v, size):
        self.set_nz(v, size)
        self.v = self.c = 0

    def add_flags(self, s, d, r, size, sub=False, setx=True):
        m, sb = MASK[size], SIGN[size]
        s, d, r = s & m, d & m, r & m
        if sub:   # r = d - s
            self.v = 1 if ((d ^ s) & (d ^ r) & sb) else 0
            self.c = 1 if s > d else 0
        else:
            self.v = 1 if (~(d ^ s) & (d ^ r) & sb) else 0
            self.c = 1 if (s + d) > m else 0
        if setx:
            self.x = self.c
        self.set_nz(r, size)

    def cond(self, cc):
        n, z, v, c = self.n, self.z, self.v, self.c
        return [True, False, not c and not z, c or z, not c, c, not z, z,
                not v, v, not n, n, n == v, n != v, (n == v) and not z, z or (n != v)][cc]

    # effective addresses ------------------------------------------------
    def ea(self, mode, reg, size):
        """Return ('d', n) | ('a', n) | ('m', addr) | ('i', value)."""
        if mode == 0:
            return ('d', reg)
        if mode == 1:
            return ('a', reg)
        if mode == 2:
            return ('m', self.a[reg])
        if mode == 3:
            addr = self.a[reg]
            self.a[reg] = (addr + (2 if reg == 7 and size == 1 else size)) & 0xFFFFFFFF
            return ('m', addr)
        if mode == 4:
            self.a[reg] = (self.a[reg] - (2 if reg == 7 and size == 1 else size)) & 0xFFFFFFFF
            return ('m', self.a[reg])
        if mode == 5:
            return ('m', (self.a[reg] + sx(self.fetch16(), 2)) & 0xFFFFFFFF)
        if mode == 6:
            return ('m', (self.a[reg] + self.index_disp()) & 0xFFFFFFFF)
        if mode == 7:
            if reg == 0:
                return ('m', sx(self.fetch16(), 2) & 0xFFFFFFFF)
            if reg == 1:
                hi = self.fetch16()
                return ('m', (hi << 16) | self.fetch16())
            if reg == 2:
                base = self.pc
                return ('m', (base + sx(self.fetch16(), 2)) & 0xFFFFFFFF)
            if reg == 3:
                base = self.pc
                return ('m', (base + self.index_disp()) & 0xFFFFFFFF)
            if reg == 4:
                if size == 4:
                    hi = self.fetch16()
                    return ('i', (hi << 16) | self.fetch16())
                return ('i', self.fetch16() & MASK[size])
        raise Stop('bad EA mode %d reg %d at pc %06X' % (mode, reg, self.pc))

    def index_disp(self):
        ext = self.fetch16()
        r = (ext >> 12) & 7
        xv = self.a[r] if ext & 0x8000 else self.d[r]
        if not ext & 0x0800:
            xv = sx(xv, 2)
        return sx(ext & 0xFF, 1) + xv

    def get(self, loc, size):
        k, v = loc
        if k == 'd':
            return self.d[v] & MASK[size]
        if k == 'a':
            return self.a[v] & MASK[size]
        if k == 'm':
            return self.read(v, size)
        return v & MASK[size]

    def put(self, loc, val, size):
        k, v = loc
        val &= MASK[size]
        if k == 'd':
            self.d[v] = (self.d[v] & ~MASK[size] & 0xFFFFFFFF) | val
        elif k == 'a':
            self.a[v] = sx(val, size) & 0xFFFFFFFF
        elif k == 'm':
            self.write(v, val, size)
        else:
            raise Stop('write to immediate at pc %06X' % self.pc)

    def push32(self, v):
        self.a[7] = (self.a[7] - 4) & 0xFFFFFFFF
        self.write(self.a[7], v, 4)

    def pop32(self):
        v = self.read(self.a[7], 4)
        self.a[7] = (self.a[7] + 4) & 0xFFFFFFFF
        return v

    # execution -----------------------------------------------------------
    def call(self, addr, sentinel=0xFFFFFE):
        self.push32(sentinel)
        self.pc = addr
        while self.pc != sentinel:
            if self.pc in self.hooks:
                self.hooks[self.pc](self)
                self.pc = self.pop32() & 0xFFFFFF
                continue
            self.step()
            self.steps += 1
            if self.steps > 2000000:
                raise Stop('runaway')

    def step(self):
        pc0 = self.pc
        op = self.fetch16()
        hi = op >> 12
        if op == 0x4E75:                      # RTS
            self.pc = self.pop32() & 0xFFFFFF
            return
        if op == 0x4E71:                      # NOP
            return
        if hi in (1, 2, 3):                   # MOVE / MOVEA
            size = {1: 1, 3: 2, 2: 4}[hi]
            src = self.get(self.ea((op >> 3) & 7, op & 7, size), size)
            dmode, dreg = (op >> 6) & 7, (op >> 9) & 7
            if dmode == 1:
                self.a[dreg] = sx(src, size) & 0xFFFFFFFF
                return
            self.put(self.ea(dmode, dreg, size), src, size)
            self.logic_flags(src, size)
            return
        if hi == 7:                           # MOVEQ
            v = sx(op & 0xFF, 1) & 0xFFFFFFFF
            self.d[(op >> 9) & 7] = v
            self.logic_flags(v, 4)
            return
        if hi == 6:                           # Bcc / BRA / BSR
            disp = op & 0xFF
            base = self.pc
            if disp == 0:
                disp = sx(self.fetch16(), 2)
            else:
                disp = sx(disp, 1)
            target = (base + disp) & 0xFFFFFF
            cc = (op >> 8) & 0xF
            if cc == 1:
                self.push32(self.pc)
                self.pc = target
            elif cc == 0 or self.cond(cc):
                self.pc = target
            return
        if hi == 5:
            size_bits = (op >> 6) & 3
            mode, reg = (op >> 3) & 7, op & 7
            if size_bits == 3:
                cc = (op >> 8) & 0xF
                if mode == 1:                 # DBcc
                    disp = sx(self.fetch16(), 2)
                    if not self.cond(cc):
                        cnt = (self.d[reg] - 1) & 0xFFFF
                        self.d[reg] = (self.d[reg] & 0xFFFF0000) | cnt
                        if cnt != 0xFFFF:
                            self.pc = (pc0 + 2 + disp) & 0xFFFFFF
                    return
                self.put(self.ea(mode, reg, 1), 0xFF if self.cond(cc) else 0, 1)   # Scc
                return
            size = (1, 2, 4)[size_bits]
            q = (op >> 9) & 7 or 8
            sub = bool(op & 0x100)
            if mode == 1:                     # ADDQ/SUBQ to An: no flags, long
                self.a[reg] = (self.a[reg] + (-q if sub else q)) & 0xFFFFFFFF
                return
            loc = self.ea(mode, reg, size)
            dv = self.get(loc, size)
            r = dv - q if sub else dv + q
            self.put(loc, r, size)
            self.add_flags(q, dv, r, size, sub)
            return
        if hi == 0:
            return self.group0(op, pc0)
        if hi == 4:
            return self.group4(op, pc0)
        if hi in (8, 9, 0xB, 0xC, 0xD):
            return self.arith(op, hi)
        if hi == 0xE:
            return self.shift(op)
        raise Stop('unhandled opcode %04X at %06X' % (op, pc0))

    def group0(self, op, pc0):
        mode, reg = (op >> 3) & 7, op & 7
        if op & 0x100 or (op & 0xFF00) == 0x0800:      # bit ops
            if op & 0x100:
                if mode == 1:
                    raise Stop('MOVEP at %06X' % pc0)
                bit = self.d[(op >> 9) & 7]
            else:
                bit = self.fetch16() & 0xFF
            kind = (op >> 6) & 3
            size = 4 if mode == 0 else 1
            bit &= 31 if size == 4 else 7
            loc = self.ea(mode, reg, size)
            v = self.get(loc, size)
            self.z = 0 if v & (1 << bit) else 1
            if kind == 1:
                self.put(loc, v ^ (1 << bit), size)
            elif kind == 2:
                self.put(loc, v & ~(1 << bit), size)
            elif kind == 3:
                self.put(loc, v | (1 << bit), size)
            return
        kind = (op >> 9) & 7
        size = (1, 2, 4, None)[(op >> 6) & 3]
        if size is None:
            raise Stop('unhandled opcode %04X at %06X' % (op, pc0))
        if op in (0x003C, 0x023C):                     # ORI/ANDI to CCR
            imm = self.fetch16() & 0x1F
            ccr = (self.x << 4) | (self.n << 3) | (self.z << 2) | (self.v << 1) | self.c
            ccr = ccr | imm if op == 0x003C else ccr & imm
            self.x, self.n, self.z, self.v, self.c = [(ccr >> s) & 1 for s in (4, 3, 2, 1, 0)]
            return
        if size == 4:
            imm = (self.fetch16() << 16) | self.fetch16()
        else:
            imm = self.fetch16() & MASK[size]
        loc = self.ea(mode, reg, size)
        dv = self.get(loc, size)
        if kind == 0:   # ORI
            r = dv | imm
            self.put(loc, r, size)
            self.logic_flags(r, size)
        elif kind == 1:  # ANDI
            r = dv & imm
            self.put(loc, r, size)
            self.logic_flags(r, size)
        elif kind == 2:  # SUBI
            r = dv - imm
            self.put(loc, r, size)
            self.add_flags(imm, dv, r, size, True)
        elif kind == 3:  # ADDI
            r = dv + imm
            self.put(loc, r, size)
            self.add_flags(imm, dv, r, size, False)
        elif kind == 5:  # EORI
            r = dv ^ imm
            self.put(loc, r, size)
            self.logic_flags(r, size)
        elif kind == 6:  # CMPI
            self.add_flags(imm, dv, dv - imm, size, True, setx=False)
        else:
            raise Stop('unhandled opcode %04X at %06X' % (op, pc0))

    def group4(self, op, pc0):
        mode, reg = (op >> 3) & 7, op & 7
        if op & 0x1C0 == 0x1C0:                        # LEA
            loc = self.ea(mode, reg, 4)
            self.a[(op >> 9) & 7] = loc[1] & 0xFFFFFFFF
            return
        if op & 0xFFC0 == 0x4E80:                      # JSR
            loc = self.ea(mode, reg, 4)
            self.push32(self.pc)
            self.pc = loc[1] & 0xFFFFFF
            return
        if op & 0xFFC0 == 0x4EC0:                      # JMP
            self.pc = self.ea(mode, reg, 4)[1] & 0xFFFFFF
            return
        if op & 0xFFF8 == 0x4840:                      # SWAP
            v = self.d[reg]
            v = ((v << 16) | (v >> 16)) & 0xFFFFFFFF
            self.d[reg] = v
            self.logic_flags(v, 4)
            return
        if op & 0xFFF8 in (0x4880, 0x48C0):            # EXT.W / EXT.L
            if op & 0x40:
                v = sx(self.d[reg], 2) & 0xFFFFFFFF
                self.d[reg] = v
                self.logic_flags(v, 4)
            else:
                v = sx(self.d[reg], 1) & 0xFFFF
                self.d[reg] = (self.d[reg] & 0xFFFF0000) | v
                self.logic_flags(v, 2)
            return
        if op & 0xFB80 == 0x4880:                      # MOVEM
            mask = self.fetch16()
            size = 4 if op & 0x40 else 2
            regs = [('d', i) for i in range(8)] + [('a', i) for i in range(8)]
            if op & 0x400:                             # memory -> registers
                if mode == 3:
                    addr = self.a[reg]
                else:
                    addr = self.ea(mode, reg, size)[1]
                for i in range(16):
                    if mask & (1 << i):
                        k, n = regs[i]
                        val = sx(self.read(addr, size), size) & 0xFFFFFFFF
                        if k == 'd':
                            self.d[n] = val if size == 4 else (self.d[n] & 0xFFFF0000) | (val & 0xFFFF)
                        else:
                            self.a[n] = val
                        addr += size
                if mode == 3:
                    self.a[reg] = addr & 0xFFFFFFFF
            else:                                      # registers -> memory
                if mode == 4:
                    addr = self.a[reg]
                    for i in range(16):
                        if mask & (1 << i):
                            k, n = regs[15 - i]
                            addr -= size
                            self.write(addr, (self.d[n] if k == 'd' else self.a[n]), size)
                    self.a[reg] = addr & 0xFFFFFFFF
                else:
                    addr = self.ea(mode, reg, size)[1]
                    for i in range(16):
                        if mask & (1 << i):
                            k, n = regs[i]
                            self.write(addr, (self.d[n] if k == 'd' else self.a[n]), size)
                            addr += size
            return
        sub = (op >> 8) & 0xF
        size = (1, 2, 4, None)[(op >> 6) & 3]
        if size is None:
            raise Stop('unhandled opcode %04X at %06X' % (op, pc0))
        loc = self.ea(mode, reg, size)
        if sub == 2:                                   # CLR
            self.put(loc, 0, size)
            self.logic_flags(0, size)
        elif sub == 4:                                 # NEG
            dv = self.get(loc, size)
            r = -dv
            self.put(loc, r, size)
            self.add_flags(dv, 0, r, size, True)
        elif sub == 6:                                 # NOT
            r = ~self.get(loc, size)
            self.put(loc, r, size)
            self.logic_flags(r, size)
        elif sub == 0xA:                               # TST
            self.logic_flags(self.get(loc, size), size)
        else:
            raise Stop('unhandled opcode %04X at %06X' % (op, pc0))

    def arith(self, op, hi):
        dn = (op >> 9) & 7
        opmode = (op >> 6) & 7
        mode, reg = op & 0x38, op & 7
        mode >>= 3
        if hi == 0xC and opmode in (3, 7):              # MULU / MULS
            src = self.get(self.ea(mode, reg, 2), 2)
            if opmode == 3:
                r = (self.d[dn] & 0xFFFF) * src
            else:
                r = sx(self.d[dn], 2) * sx(src, 2)
            r &= 0xFFFFFFFF
            self.d[dn] = r
            self.logic_flags(r, 4)
            return
        if hi in (9, 0xB, 0xD) and opmode in (3, 7):    # SUBA / CMPA / ADDA
            size = 2 if opmode == 3 else 4
            src = sx(self.get(self.ea(mode, reg, size), size), size)
            av = self.a[dn]
            if hi == 0xD:
                self.a[dn] = (av + src) & 0xFFFFFFFF
            elif hi == 9:
                self.a[dn] = (av - src) & 0xFFFFFFFF
            else:
                self.add_flags(src, av, av - src, 4, True, setx=False)
            return
        if hi == 8 and opmode in (3, 7):
            raise Stop('DIVU/DIVS not supported')
        size = (1, 2, 4)[opmode & 3]
        to_ea = bool(opmode & 4)
        if hi == 0xB and to_ea:                         # EOR Dn,<ea>
            loc = self.ea(mode, reg, size)
            r = self.get(loc, size) ^ self.d[dn]
            self.put(loc, r, size)
            self.logic_flags(r, size)
            return
        if to_ea and mode in (0, 1) and hi in (9, 0xD, 0xC, 8):
            raise Stop('ADDX/SUBX/ABCD/EXG not supported')
        loc = self.ea(mode, reg, size)
        ev = self.get(loc, size)
        dv = self.d[dn] & MASK[size]
        if to_ea:
            s, d = dv, ev
        else:
            s, d = ev, dv
        if hi == 0xD:
            r = d + s
            self.add_flags(s, d, r, size, False)
        elif hi == 9:
            r = d - s
            self.add_flags(s, d, r, size, True)
        elif hi == 0xB:                                  # CMP
            self.add_flags(s, d, d - s, size, True, setx=False)
            return
        elif hi == 0xC:
            r = d & s
            self.logic_flags(r, size)
        else:
            r = d | s
            self.logic_flags(r, size)
        if to_ea:
            self.put(loc, r, size)
        else:
            self.put(('d', dn), r, size)

    def shift(self, op):
        size_bits = (op >> 6) & 3
        if size_bits == 3:
            raise Stop('memory shift not supported')
        size = (1, 2, 4)[size_bits]
        left = bool(op & 0x100)
        kind = (op >> 3) & 3
        reg = op & 7
        cnt = (op >> 9) & 7
        if op & 0x20:
            cnt = self.d[cnt] & 63
        elif cnt == 0:
            cnt = 8
        m, sb = MASK[size], SIGN[size]
        v = self.d[reg] & m
        self.v = 0
        if cnt == 0:
            self.c = 0
            self.logic_flags(v, size)
            return
        if kind in (0, 1):                               # ASx / LSx
            for _ in range(cnt):
                if left:
                    c = 1 if v & sb else 0
                    nv = (v << 1) & m
                    if kind == 0 and ((nv ^ v) & sb):
                        self.v = 1
                else:
                    c = v & 1
                    nv = (v >> 1) | (v & sb if kind == 0 else 0)
                v = nv
                self.c = self.x = c
        elif kind == 3:                                  # ROx
            for _ in range(cnt):
                if left:
                    c = 1 if v & sb else 0
                    v = ((v << 1) | c) & m
                else:
                    c = v & 1
                    v = (v >> 1) | (sb if c else 0)
                self.c = c
        else:
            raise Stop('ROXx not supported')
        self.set_nz(v, size)
        self.d[reg] = (self.d[reg] & ~m & 0xFFFFFFFF) | v


# --------------------------------------------------------------------------

def effect_pages(rom, e):
    """Parameter descriptors of effect e in the order 0xC0FA70 visits them."""
    rec = FX_RECORDS + 16 * e
    pageset = rom.b(rec + 4)
    ps = RAMCOPY_SRC + (PAGESET_RAM + 4 * pageset - RAMCOPY_DST)
    plist, npages = rom_ptr(rom.w(ps)), rom.b(ps + 3)
    out = []
    for pg in range(npages):
        page = RAMCOPY_SRC + ((0xFF0000 | rom.w(plist + 2 * pg)) - RAMCOPY_DST)
        ptr, cnt = rom.w(page), rom.b(page + 3)
        descs = [rom_ptr(ptr)] if cnt <= 1 else [rom_ptr(rom.w(rom_ptr(ptr) + 2 * i)) for i in range(cnt)]
        for d in descs:
            out.append({'page': pg, 'desc': d, 'offset': rom.w(d), 'name_ptr': rom_ptr(rom.w(d + 6)),
                        'handler': rom.w(d + 8), 'flags': rom.b(d + 4)})
    return out


TOKENS = 0xC05972


def decode_text(rom, a, depth=0):
    """Display text: 0x9E nn = token nn (word table 0xC05972), 0x9F = end."""
    s = ''
    while True:
        c = rom.b(a)
        a += 1
        if c == 0x9F:
            return s
        if c == 0x9E:
            s += decode_text(rom, 0xC00000 | rom.w(TOKENS + 2 * rom.b(a)), depth + 1)
            a += 1
        elif 32 <= c < 127:
            s += chr(c)
        else:
            s += ' '


def run_effect(rom, e):
    """Re-run the firmware's effect set-up for effect e; return ordered GPR writes."""
    cpu = Cpu(rom)
    # RAM copy done at boot by 0xC11938 (page tables live there)
    for i in range(0x560):
        cpu.ram[(RAMCOPY_DST + i) & 0xFFFF] = rom.b(RAMCOPY_SRC + i)
    rec = FX_RECORDS + 16 * e
    for k in range(10):                    # 0xC0F9F6: defaults -> block bytes 0,2,..,0x12
        cpu.ram[(FX_BLOCK + 2 * k) & 0xFFFF] = rom.b(rec + 6 + k)
    cpu.ram[(FX_BLOCK + 0x14) & 0xFFFF] = e
    cpu.write(MASTER_VOL, MASTER_VOL_INDEX, 2)
    cpu.written.clear()
    cpu.a[7] = 0xFFF000
    writes = []
    phase = ['init routine 0x%06X' % (0xC10000 | rom.w(rec))]

    def host_write(c):
        writes.append((c.d[1] & 0xFF, c.d[2] & 0xFFFFFF, phase[0]))
        # register state 0xC1CC80 leaves behind (successful first-try write):
        # D2 = read-back value, D4 = 0x100, D0 = dbeq counter, A0 = ESP base
        c.d[2] &= 0xFFFFFF
        c.d[4] = (c.d[4] & 0xFFFF0000) | 0x100
        c.d[0] = 0x0001001E
        c.a[0] = 0x260001

    cpu.hooks[HOST_WRITE] = host_write
    cpu.hooks[HALT_RELEASE] = lambda c: writes.append((None, None, 'halt_release'))
    cpu.a[3] = FX_BLOCK
    cpu.call(0xC10000 | rom.w(rec))                   # effect init routine
    for p in effect_pages(rom, e):                     # 0xC0FA70
        h = p['handler']
        if h in (0, 0x989A) or p['flags'] & 0x80:
            continue
        phase[0] = 'block+%02X handler 0x%06X' % (p['offset'], 0xC10000 | h)
        cpu.a[3] = FX_BLOCK
        cpu.call(0xC10000 | h)
    phase[0] = 'mix'
    cpu.call(MIX_ROUTINE)                              # 0xC1CB76
    return writes, cpu


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('rom_dir', nargs='?', default='ks-32')
    ap.add_argument('--out', default='docs/rom-investigation/data/esp/effect_defaults.json')
    args = ap.parse_args()
    rom = Rom(load_rom(args.rom_dir))

    # 1. FC table ---------------------------------------------------------
    fc = [rom.w(FC_TABLE + 2 * i) for i in range(768)]
    diffs = [(i, fc[i], round(1024 * 2 ** (i / 768))) for i in range(768) if fc[i] != round(1024 * 2 ** (i / 768))]
    print('FC table 0x%06X: %d entries, 0x%03X..0x%03X, %d differences from round(1024*2^(i/768))'
          % (FC_TABLE, len(fc), fc[0], fc[-1], len(diffs)))
    for d in diffs:
        print('  i=%d rom=%d formula=%d' % d)

    # 2. level tables -----------------------------------------------------
    mix = [rom.w(MIX_TABLE + 2 * i) for i in range(128)]
    vol = [rom.w(VOL_TABLE + 2 * i) for i in range(128)]
    sine_ok = all(mix[i] == round(32767 * math.sin(i * math.pi / 254)) for i in range(128))
    print('mix table 0x%06X == round(32767*sin(i*pi/254)) for all 128: %s' % (MIX_TABLE, sine_ok))
    for name, base, t in (('mix', MIX_TABLE, mix), ('volume', VOL_TABLE, vol)):
        print('%s table 0x%06X:' % (name, base))
        for r in range(0, 128, 16):
            print('  %3d: %s' % (r, ' '.join('%04X' % v for v in t[r:r + 16])))

    # 3. effects ------------------------------------------------------------
    prog_ptrs = [rom.l(PROG_PTRS + 4 * i) for i in range(N_PROGRAMS)]
    prog_regs = []                 # registers named in any operand field of each program
    for ptr in prog_ptrs:
        area = bytes(rom.img[0x20000::2])          # upper EPROM 0x10000.. (byte-wide area)
        instr = decode_program(area, ptr - 0xC20000)[2]
        prog_regs.append({(w >> sh) & 0xFF for w in instr.values() for sh in (40, 32, 24, 16)})
    effects = []
    for e in range(N_EFFECTS):
        rec = FX_RECORDS + 16 * e
        writes, cpu = run_effect(rom, e)
        pages = effect_pages(rom, e)
        effects.append({'effect': e, 'prog': rom.b(rec + 5), 'writes': writes, 'pages': pages,
                        'defaults': [rom.b(rec + 6 + k) for k in range(10)],
                        'de8a': cpu.ram[0xDE8A], 'de8a_set': 0xDE8A in cpu.written})
        print('effect %2d -> ESP program %2d, init 0x%06X, defaults %s'
              % (e, rom.b(rec + 5), 0xC10000 | rom.w(rec),
                 ' '.join('%02X' % b for b in effects[-1]['defaults'])))
        for p in pages:
            print('    page %d desc 0x%06X block+%02X %-28s handler %s'
                  % (p['page'], p['desc'], p['offset'], decode_text(rom, p['name_ptr']) if p['name_ptr'] != 0xC10000 else '(no name)',
                     '0x%06X' % (0xC10000 | p['handler']) if p['handler'] not in (0, 0x989A) else '-'))
        for g, v, ph in writes:
            if g is not None:
                print('    %-28s GPR %02X <- %06X' % (ph, g, v))
            else:
                print('    ESPHALT released')

    # 4. output JSON: one entry per ESP program index (lowest effect number using it)
    programs = []
    for idx in range(N_PROGRAMS):
        users = [fx for fx in effects if fx['prog'] == idx]
        fx = users[0]
        final, basis, order = {}, {}, []
        for g, v, ph in fx['writes']:
            if g is None:
                continue
            if g not in final:
                order.append(g)
            final[g] = v
            basis[g] = ph
        e = fx['effect']
        rec = FX_RECORDS + 16 * e
        d = fx['defaults']
        idx_of = {0x7C: d[8], 0x7D: d[8], 0x7A: d[9], 0x7B: d[9], 0x76: fx['de8a'], 0x77: fx['de8a']}
        gpr, why = {}, {}
        keys = [0x7F, 0x7C, 0x7D, 0x7A, 0x7B, 0x76, 0x77] + sorted(g for g in order if g not in (0x7F, 0x7C, 0x7D, 0x7A, 0x7B, 0x76, 0x77))
        for g in keys:
            if g not in final:
                continue
            gpr['%02X' % g] = '%06X' % final[g]
            unused = '' if g in prog_regs[idx] else '; not referenced by ESP program %d' % idx
            if g == 0x7F:
                why['7F'] = ('firmware fade target word[0x%06X + 2*$DD72] << 8, ramped by 0xC1CC1A; '
                             'hand-set index $DD72 = %d (master volume slider at maximum)' % (VOL_TABLE, MASTER_VOL_INDEX))
            elif g in (0x76, 0x77) and not fx['de8a_set']:
                why['%02X' % g] = ('table 0x%06X entry %d written by 0x%06X with index $DE8A, which effect %d does '
                                   'not set (the firmware keeps the value left by an earlier effect); hand-set index 0'
                                   % (MIX_TABLE, 0 if g == 0x76 else 127, MIX_ROUTINE, e))
            elif g in idx_of:
                n = idx_of[g]
                ent = n if g in (0x7C, 0x7A, 0x76) else 127 - n
                de8a_by = ('block+0C handler 0xC1D18E' if any(w[0] == 0x77 and w[2] != 'mix' for w in fx['writes'])
                           else 'init routine 0x%06X' % (0xC10000 | rom.w(rec)))
                src = {0x7C: 'block+10 (FX1)', 0x7D: 'block+10 (FX1)', 0x7A: 'block+12 (FX2)',
                       0x7B: 'block+12 (FX2)', 0x76: '$DE8A set by ' + de8a_by,
                       0x77: '$DE8A set by ' + de8a_by}[g]
                why['%02X' % g] = ('table 0x%06X entry %d (index %s = 0x%02X; %s), written by 0x%06X; '
                                   'index from the default parameters of effect %d (record 0x%06X)'
                                   % (MIX_TABLE, ent, src, n, 'sine' if ent == n else '127 - index, cosine',
                                      MIX_ROUTINE, e, rec))
            else:
                why['%02X' % g] = ('firmware %s (last write) for effect %d with its default parameters (record 0x%06X)'
                                   % (basis[g], e, rec))
            why['%02X' % g] += unused
        programs.append({'index': idx, 'name': None, 'effect': e,
                         'also_used_by_effects': [u['effect'] for u in users[1:]],
                         'pointer': '0x%06X' % prog_ptrs[idx],
                         'gpr': gpr, 'basis': why})

    # GPR 7F writes of the fade-up (counted from the interpreted 0xC1CB76 run; the same for every effect)
    counts = {sum(1 for w in fx['writes'] if w[0] == 0x7F) for fx in effects}
    assert len(counts) == 1, counts
    ramp_writes = counts.pop()
    print('fade-up: %d writes to GPR 7F per effect load' % ramp_writes)
    out = {
        'source': ('KS-32 OS v3.01; generated by docs/rom-investigation/tools/extract_effect_params.py, '
                   'which re-runs the firmware effect set-up (init routine from 0x%06X, parameter handlers '
                   'via 0xC0FA70, mix/fade 0x%06X) with each effect\'s default parameters; see '
                   'docs/rom-investigation/effect-params.md' % (FX_RECORDS, MIX_ROUTINE)),
        'fade': {'gpr': '7F', 'frames': ramp_writes * TRANSFERS_PER_WRITE * FRAMES_PER_TRANSFER,
                 'from': '000000', 'to': '%06X' % (vol[MASTER_VOL_INDEX] << 8),
                 'step': '%06X' % (RAMP_7F_STEP << 8), 'writes': ramp_writes,
                 'basis': ('0xC1CC1A ramps GPR 7F from 0 in steps of 0x12F (word) = 0x012F00, clamps at 0x7FFF '
                           'and rewrites the target: %d verified writes x %d host transfers x %d frames per '
                           'transfer (one transfer per ESP sample, 68000 misses the PC<0x28 window); '
                           'estimate, see effect-params.md' % (ramp_writes, TRANSFERS_PER_WRITE, FRAMES_PER_TRANSFER))},
        'programs': programs,
    }
    text = json.dumps(out, indent=1) + '\n'
    with open(args.out, 'w', newline='\n') as f:
        f.write(text)
    print('wrote', args.out)


if __name__ == '__main__':
    main()
