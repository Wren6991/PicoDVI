#!/usr/bin/env python3

# A script to generate TMDS lookup tables for pairs of 4bpp pixels.
#
# All TMDS symbol pairs generated are DC balanced. The script uses a
# search to find the DC balanced symbol pair with the least error
# compared with the desired pixels.

def popcount(x):
    n = 0
    while x:
        n += 1
        x = x & (x - 1)
    return n

# Equivalent to N1(q) - N0(q) in the DVI spec
def byteimbalance(x):
    return 2 * popcount(x) - 8

class TMDSEncode:
    ctrl_syms = {
        0b00: 0b1101010100,
        0b01: 0b0010101011,
        0b10: 0b0101010100,
        0b11: 0b1010101011
    }
    def __init__(self):
        self.imbalance = 0

    def encode(self, d, c, de):
        if not de:
            self.imbalance = 0
            return self.ctrl_syms[c]
        # Minimise transitions
        q_m = d & 0x1
        if popcount(d) > 4 or (popcount(d) == 4 and not d & 0x1):
            for i in range(7):
                q_m = q_m | (~(q_m >> i ^ d >> i + 1) & 0x1) << i + 1
        else:
            for i in range(7):
                q_m = q_m | ( (q_m >> i ^ d >> i + 1) & 0x1) << i + 1
            q_m = q_m | 0x100
        # Correct DC balance
        inversion_mask = 0x2ff
        q_out = 0
        if self.imbalance == 0 or byteimbalance(q_m & 0xff) == 0:
            q_out = q_m ^ (0 if q_m & 0x100 else inversion_mask)
            if q_m & 0x100:
                self.imbalance += byteimbalance(q_m & 0xff)
            else:
                self.imbalance -= byteimbalance(q_m & 0xff)
        elif (self.imbalance > 0) == (byteimbalance(q_m & 0xff) > 0):
            q_out = q_m ^ inversion_mask
            self.imbalance += ((q_m & 0x100) >> 7) - byteimbalance(q_m & 0xff)
        else:
            q_out = q_m
            self.imbalance += byteimbalance(q_m & 0xff) - ((~q_m & 0x100) >> 7)
        return q_out

def gen_4bpp_lut():
    e = TMDSEncode()
    for i in range(256):
        g0 = (i % 16) * 17
        g1 = (i // 16) * 17
        dithpats = ((0, 0), (0, 1), (0, -1), (1, 0), (-1, 0), (1, -1), (-1, 1),
            (2, -2), (-2, 2), (2, -1), (-2, 1), (1, -2), (-1, 2),
            (2, 0), (-2, 0), (0, 2), (0, -2),
            (2, 1), (-2, -1), (1, 2), (-1, -2), (2, 2), (-2, -2),
            (3, -3), (-3, 3), (3, -2), (-3, 2), (2, -3), (-2, 3),
            (3, -1), (-3, 1), (1, -3), (-1, 3),
            (3, 0), (-3, 0), (0, 3), (0, -3),
            (3, 1), (-3, -1), (-1, -3), (1, 3),
            #(3, 2), (-3, -2), (-2, -3), (2, 3), (3, 3), (-3, -3),
            (4, -4), (-4, 4), (4, -3), (-4, 3), (3, -4), (-3, 4),
            (4, -2), (-4, 2), (2, -4), (-4, 2),
            (4, -1), (-4, 1), (1, -4), (-1, 4),
            (4, 0), (-4, 0), (0, 4), (0, -4),
            #(4, 1), (-4, -1), (-1, -4), (1, 4),
            #(4, 2), (-4, -2), (-2, -4), (2, 4),
            #(4, 3), (-4, -3), (-3, -4), (3, 4), (4, 4), (-4, -4),
            (5, -5), (-5, 5), (5, -4), (-5, 4), (4, -5), (-4, 5),
            (5, -3), (-5, 3), (3, -5), (-5, 3),
            (5, -2), (-5, 2), (2, -5), (-5, 2),
            )
        found = False
        for a, b in dithpats:
            h0 = g0 + a
            h1 = g1 + b
            if h0 < 0 or h0 > 255 or h1 < 0 or h1 > 255:
                continue
            e.imbalance = 0
            t0 = e.encode(h0, 0, True)
            t1 = e.encode(h1, 0, True)
            if e.imbalance == 0:
                word = (t1 << 10) | t0
                #print(f'\t.word 0x{word:05x} // {h0:2x}, {h1:2x} ({a:+}, {b:+})')
                print(f'0x{word:05x}, // {h0:2x}, {h1:2x} ({a:+}, {b:+})')
                found = True
                break
        if not found:
            print(f'error {g0:2x} {g1:2x}')

gen_4bpp_lut()
