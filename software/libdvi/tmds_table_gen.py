#!/usr/bin/env python3

# The key fact is that, if x is even, and the encoder currently has a running
# imbalance of 0, encoding x followed by x + 1 produces a symbol pair with a
# net balance of 0.
#
# This is a reasonable constraint, because we only want RGB565 (so 6 valid
# channel data bits -> data is multiple of 4), and can probably tolerate
# 0.25LSB of noise :)
#
# This means that encoding a half-horizontal-resolution scanline buffer is a
# simple LUT operation for each colour channel, because we have made the
# encoding process stateless by guaranteeing 0 balance.

def popcount(x):
	n = 0
	while x:
		n += 1
		x = x & (x - 1)
	return n

# Equivalent to N1(q) - N0(q) in the DVI spec
def byteimbalance(x):
	return 2 * popcount(x) - 8

# This is a direct translation of "Figure 3-5. T.M.D.S. Encode Algorithm" on
# page 29 of DVI 1.0 spec

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

# Turn a bitmap of width n into n pairs of pseudo-differential bits
def differentialise(x, n):
	accum = 0
	for i in range(n):
		accum <<= 2
		if x & (1 << (n - 1)):
			accum |= 0b01
		else:
			accum |= 0b10
		x <<= 1
	return accum

enc = TMDSEncode()


def disptable_format(sym):
	return differentialise(sym, 10) | ((popcount(sym) * 2 - 10 & 0x3f) << 26)

print("// Non-negative running disparity:")
for i in range(0, 256, 4):
	enc.imbalance = 1
	print("0x{:08x},".format(disptable_format(enc.encode(i, 0, 1))))

print("// Negative running disparity:")
for i in range(0, 256, 4):
	enc.imbalance = -1
	print("0x{:08x},".format(disptable_format(enc.encode(i, 0, 1))))


# for i in range(4):
# 	print("0x{:05x},".format(differentialise(enc.encode(0, i, 0), 10)))

