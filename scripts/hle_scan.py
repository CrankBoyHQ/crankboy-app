#!/usr/bin/env python3
"""Static HLE poll-loop scanner for Game Boy (Color) ROMs.

Detects tight poll loops (IO load -> flag compare -> conditional branch back)
and diffs them against the HLE analyzer's supported matrix (peanut_gb.h
__gb_hle_analyze) to surface common missed patterns.

Usage: python3 hle_scan.py GLOB...
"""
import glob
import sys
from collections import Counter, defaultdict

# ---------------------------------------------------------------------------
# SM83 disassembler. Returns (length, mnemonic) where mnemonic carries enough
# info to classify loads / compares / branches.
# ---------------------------------------------------------------------------

R = ['b', 'c', 'd', 'e', 'h', 'l', '(hl)', 'a']
RP = ['bc', 'de', 'hl', 'sp']
CC = {0x20: 'nz', 0x28: 'z', 0x30: 'nc', 0x38: 'c'}


def disasm(buf, pc):
    op = buf[pc]
    n = len(buf)

    def b8(o):
        return buf[o] if o < n else 0

    def b16(o):
        return (b8(o) | (b8(o + 1) << 8)) if o + 1 < n else 0

    # CB prefix
    if op == 0xCB:
        cb = b8(pc + 1)
        x, y, z = cb & 7, (cb >> 3) & 7, cb >> 6
        if z == 0:
            m = ['rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', 'swap', 'srl'][y]
            return 2, f'cb {m} {R[x]}'
        m = ['bit', 'res', 'set'][z - 1]
        return 2, f'cb {m} {y},{R[x]}'

    if op == 0x00:
        return 1, 'nop'
    if op == 0x10:
        return 1, 'stop'
    if op == 0x76:
        return 1, 'halt'
    if op == 0xF3:
        return 1, 'di'
    if op == 0xFB:
        return 1, 'ei'

    # 8-bit ALU with A
    if 0x80 <= op <= 0xBF:
        base = (op >> 3) & 7
        m = {0: 'add', 1: 'adc', 2: 'sub', 3: 'sbc', 4: 'and', 5: 'xor', 6: 'or', 7: 'cp'}[base]
        return 1, f'{m} a,{R[op & 7]}'

    # 8-bit load r, r'
    if 0x40 <= op <= 0x7F:
        dst = (op >> 3) & 7
        src = op & 7
        if dst == 6 and src == 6:
            return 1, 'halt'  # 0x76 handled above
        return 1, f'ld {R[dst]},{R[src]}'

    # immediate 8-bit ALU
    if op in (0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE):
        m = {0xC6: 'add', 0xCE: 'adc', 0xD6: 'sub', 0xDE: 'sbc',
             0xE6: 'and', 0xEE: 'xor', 0xF6: 'or', 0xFE: 'cp'}[op]
        return 2, f'{m} a,${b8(pc + 1):02x}'

    # rotates / acc ops
    if op in (0x07, 0x0F, 0x17, 0x1F):
        return 1, {0x07: 'rlca', 0x0F: 'rrca', 0x17: 'rla', 0x1F: 'rra'}[op]
    if op == 0x27:
        return 1, 'daa'
    if op == 0x2F:
        return 1, 'cpl'
    if op == 0x37:
        return 1, 'scf'
    if op == 0x3F:
        return 1, 'ccf'

    # inc/dec reg
    if op & 0xC7 in (0x04, 0x05):
        return 1, ('inc ' if op & 0x08 else 'dec ') + R[(op >> 3) & 7]

    # jr
    if op == 0x18:
        d = b8(pc + 1)
        d = d - 256 if d >= 128 else d
        return 2, f'jr ${pc + 2 + d:04x}'
    if op in CC:
        d = b8(pc + 1)
        d = d - 256 if d >= 128 else d
        return 2, f'jr {CC[op]},${pc + 2 + d:04x}'

    # jp / call / ret
    if op == 0xC3:
        return 3, f'jp ${b16(pc + 1):04x}'
    if op in (0xC2, 0xCA, 0xD2, 0xDA):
        cc = {0xC2: 'nz', 0xCA: 'z', 0xD2: 'nc', 0xDA: 'c'}[op]
        return 3, f'jp {cc},${b16(pc + 1):04x}'
    if op == 0xCD:
        return 3, f'call ${b16(pc + 1):04x}'
    if op == 0xC9:
        return 1, 'ret'
    if op in (0xC0, 0xC8, 0xD0, 0xD8):
        cc = {0xC0: 'nz', 0xC8: 'z', 0xD0: 'nc', 0xD8: 'c'}[op]
        return 1, f'ret {cc}'
    if op == 0xD9:
        return 1, 'reti'

    # 16-bit loads / add
    if op in (0x01, 0x11, 0x21, 0x31):
        return 3, f'ld {RP[(op >> 4) & 3]},${b16(pc + 1):04x}'
    if op == 0x08:
        return 3, f'ld (${b16(pc + 1):04x}),sp'
    if op in (0x09, 0x19, 0x29, 0x39):
        return 1, f'add hl,{RP[(op >> 4) & 3]}'
    if op == 0xE8:
        return 2, f'add sp,${b8(pc + 1):02x}'
    if op == 0xF8:
        return 2, f'ld hl,sp+${b8(pc + 1):02x}'
    if op == 0xF9:
        return 1, 'ld sp,hl'

    # push/pop
    if op in (0xC5, 0xD5, 0xE5, 0xF5):
        return 1, 'push ' + {0xC5: 'bc', 0xD5: 'de', 0xE5: 'hl', 0xF5: 'af'}[op]
    if op in (0xC1, 0xD1, 0xE1, 0xF1):
        return 1, 'pop ' + {0xC1: 'bc', 0xD1: 'de', 0xE1: 'hl', 0xF1: 'af'}[op]

    # 8-bit immediate loads
    if op in (0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E):
        reg = {0x06: 0, 0x0E: 1, 0x16: 2, 0x1E: 3, 0x26: 4, 0x2E: 5, 0x36: 6, 0x3E: 7}[(op & 0x38) | (op & 0x07)]
        return 2, f'ld {R[reg]},${b8(pc + 1):02x}'

    # (hl) loads
    if op in (0x2A, 0x3A, 0x22, 0x32):
        m = {0x2A: 'ldi a,(hl)', 0x3A: 'ldd a,(hl)', 0x22: 'ldi (hl),a', 0x32: 'ldd (hl),a'}[op]
        return 1, m
    if op == 0x0A:
        return 1, 'ld a,(bc)'
    if op == 0x1A:
        return 1, 'ld a,(de)'
    if op == 0x02:
        return 1, 'ld (bc),a'
    if op == 0x12:
        return 1, 'ld (de),a'

    # IO / absolute loads
    if op == 0xE0:
        return 2, f'ldh (${b8(pc + 1):02x}),a'
    if op == 0xF0:
        return 2, f'ldh a,(${b8(pc + 1):02x})'
    if op == 0xE2:
        return 1, 'ldh (c),a'
    if op == 0xF2:
        return 1, 'ldh a,(c)'
    if op == 0xEA:
        return 3, f'ld (${b16(pc + 1):04x}),a'
    if op == 0xFA:
        return 3, f'ld a,(${b16(pc + 1):04x})'

    return 1, f'?{op:02x}'


# ---------------------------------------------------------------------------
# Loop detection.
# ---------------------------------------------------------------------------

# IO loads into A that the analyzer recognizes (load -> A). Returns the byte
# length + a label, or None.
def load_form(buf, pc):
    op = buf[pc]
    if op == 0xF0:
        return 2, 'ldh a,(n)'
    if op == 0xFA:
        return 3, 'ld a,(nn)'
    if op == 0xF2:
        return 1, 'ldh a,(c)'
    if op == 0x7E:
        return 1, 'ld a,(hl)'
    if op == 0x2A:
        return 1, 'ldi a,(hl)'
    if op == 0x3A:
        return 1, 'ldd a,(hl)'
    if op == 0xCB and (buf[pc + 1] & 0xC7) == 0x46:
        return 2, 'bit n,(hl)'
    return None


# Classify the compare instructions between load end and the branch, as a
# normalized shape string.
def shape(gap):
    out = []
    i = 0
    while i < len(gap):
        b = gap[i]
        if b == 0xCB and i + 1 < len(gap):
            cb = gap[i + 1]
            z = cb >> 6
            y = (cb >> 3) & 7
            x = cb & 7
            if z == 1:
                out.append('bit%d,r%d' % (y, x))
            elif cb == 0x37:
                out.append('swap a')
            elif z == 0:
                out.append({0x07: 'rlc a', 0x0F: 'rrc a', 0x17: 'rl a', 0x1F: 'rr a',
                            0x27: 'sla a', 0x2F: 'sra a', 0x3F: 'srl a'}.get(cb, 'cb%02x' % cb))
            else:
                out.append('cb%02x' % cb)
            i += 2
            continue
        if b in (0xA7, 0xB7, 0x3C, 0x3D, 0x2F, 0x07, 0x0F, 0x17, 0x1F, 0x37, 0x3F):
            out.append({0xA7: 'and a', 0xB7: 'or a', 0x3C: 'inc a', 0x3D: 'dec a', 0x2F: 'cpl',
                        0x07: 'rlca', 0x0F: 'rrca', 0x17: 'rla', 0x1F: 'rra', 0x37: 'scf', 0x3F: 'ccf'}[b])
            i += 1
            continue
        if b in (0xFE, 0xD6, 0xE6, 0xF6, 0xEE, 0xC6, 0xCE, 0xDE):
            out.append({0xFE: 'cp n', 0xD6: 'sub n', 0xE6: 'and n', 0xF6: 'or n',
                        0xEE: 'xor n', 0xC6: 'add n', 0xCE: 'adc n', 0xDE: 'sbc n'}[b])
            i += 2
            continue
        if 0x80 <= b <= 0xBF:
            m = ['add', 'adc', 'sub', 'sbc', 'and', 'xor', 'or', 'cp'][(b >> 3) & 7]
            out.append('%s r%d' % (m, b & 7))
            i += 1
            continue
        out.append('%02x' % b)
        i += 1
    return '+'.join(out)


# Supported-by-analyzer predicate (peanut_gb.h __gb_hle_analyze).
_SUPPORTED_SINGLE = {'and a', 'or a', 'cp n', 'sub n', 'and n',
                     'cp r0', 'cp r1', 'cp r2', 'cp r3', 'cp r4', 'cp r5',
                     'add r7'}
# and/or/xor/sub a,r (r = b,c,d,e,h,l; (hl) and a forms excluded at decode)
_SUPPORTED_SINGLE |= {'%s r%d' % (m, r) for m in ('and', 'or', 'xor', 'sub')
                      for r in (0, 1, 2, 3, 4, 5)}
# gap-position CB bit: analyzer decodes BIT n,A only (r7)
_SUPPORTED_SINGLE |= {'bit%d,r7' % i for i in range(8)}
_SUPPORTED_COMBO = {'and n+cp n', 'and n+sub n', 'and n+xor n',
                    'sub n+cp n', 'and n+or a', 'and n+dec a',
                    'dec a+cp n', 'cpl+and n'}
_SUPPORTED_COMBO |= {'sub r%d+cp n' % r for r in (0, 1, 2, 3, 4, 5)}


def is_covered(name, s):
    # bit n,(hl) with empty gap is the combined load+compare (HLE_CMP_BIT_HL)
    if name == 'bit n,(hl)' and s == '':
        return True
    if s in _SUPPORTED_COMBO:
        return True
    return s in _SUPPORTED_SINGLE


def scan(buf, jr, jp, samples):
    n = len(buf)
    for i in range(n - 3):
        lf = load_form(buf, i)
        if not lf:
            continue
        ln, name = lf
        end = i + ln
        for j in range(end, min(end + 8, n - 2)):
            br = buf[j]
            if br in CC:
                o = buf[j + 1]
                d = o - 256 if o >= 128 else o
                if j + 2 + d == i:
                    s = shape(buf[end:j])
                    jr[(name, s, CC[br])] += 1
                    samples[('jr', name, s, CC[br])].append(i)
            elif br in (0xC2, 0xCA, 0xD2, 0xDA) and j + 2 < n:
                tgt = buf[j + 1] | (buf[j + 2] << 8)
                if tgt == i:
                    cc = {0xC2: 'nz', 0xCA: 'z', 0xD2: 'nc', 0xDA: 'c'}[br]
                    s = shape(buf[end:j])
                    jp[(name, s, cc)] += 1
                    samples[('jp', name, s, cc)].append(i)


def main():
    patterns = sys.argv[1:] or glob.glob('Games/*.gbc') + glob.glob('Games/*.gb')
    jr = Counter()
    jp = Counter()
    samples = defaultdict(list)
    for p in patterns:
        with open(p, 'rb') as f:
            b = f.read()
        scan(b, jr, jp, samples)

    print("=== JR loops ===")
    print("total:", sum(jr.values()))
    covered = 0
    missed = Counter()
    for (name, s, br), v in jr.most_common():
        if is_covered(name, s):
            covered += v
        else:
            missed[(name, s, br)] += v
    print("covered:", covered, " missed:", sum(missed.values()))
    print("\nTop missed JR:")
    for (name, s, br), v in missed.most_common(40):
        print(f"  {v:6d}  {name:12s} {s:20s} jr {br}")

    print("\n=== JP loops ===")
    print("total:", sum(jp.values()))
    covered = 0
    missed = Counter()
    for (name, s, br), v in jp.most_common():
        if is_covered(name, s):
            covered += v
        else:
            missed[(name, s, br)] += v
    print("covered:", covered, " missed:", sum(missed.values()))
    print("\nTop missed JP:")
    for (name, s, br), v in missed.most_common(40):
        print(f"  {v:6d}  {name:12s} {s:20s} jp {br}")


if __name__ == '__main__':
    main()
