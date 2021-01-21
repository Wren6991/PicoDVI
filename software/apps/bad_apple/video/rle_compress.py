#!/usr/bin/env python3

import sys
from PIL import Image
import functools
import os
import struct

# We avoid TMDS encode on the M0+ by holding the following sequences as
# prebalanced symbol pairs:
#
# - Black, black
# - Black, white
# - White, black
# - White, white
#
# A sequence of these pairs can encode any black and white image, so we use
# this byte encoding:
#
# 7      0
# abnnnnnn
#
# where a, b are the colours of the two pixels in the pair, and n is a repeat
# count between 1 and 64. We can expand this into TMDS scanlines very quickly
# on the M0+ and, because everything stays byte-aligned, it's amenable to
# further compression.

def fmt_byte(a, b, n):
	assert(n >= 1 and n <= 64)
	return a << 7 | b << 6 | (n - 1 & 0x3f)

@functools.lru_cache
def pack_row(row):
	current = (False, False)
	current_len = 0
	pack = []
	for x in range(0, len(row), 2):
		pix = row[x:x+2]
		if pix == current or current_len == 0:
			current = pix
			current_len += 1
		if pix != current:
			pack.append(fmt_byte(*current, current_len))
			current = pix
			current_len = 1
		if current_len >= 64:
			pack.append(fmt_byte(*current, current_len))
			current_len = 0
	# Flush run at end of scanline
	if current_len > 0:
		pack.append(fmt_byte(*current, current_len))
	return bytes(pack)


class RowHistory:

	def __init__(self, log_size):
		self.size = 1 << log_size
		# Ring buffer of last n rows we saw
		self.history = [None] * self.size
		self.ptr = 0
		self.row_occurrences = {}
	
	def update(self, newrow):
		# First update lookups for the row we are about to evict. Note we evict rows
		# one slot in *advance* (reducing effective dict size by 1) to avoid
		# returning references to the slot that the new row is written to.
		oldrow = self.history[(self.ptr + 1) % self.size]
		if oldrow is not None:
			count, lastseen = self.row_occurrences[oldrow]
			if count == 1:
				self.row_occurrences.pop(oldrow)
			else:
				self.row_occurrences[oldrow] = (count - 1, lastseen)
		self.history[self.ptr] = newrow
		# Then update reference count and last-seen position for the new occupant
		if newrow in self.row_occurrences:
			self.row_occurrences[newrow] = (self.row_occurrences[newrow][0] + 1, self.ptr)
		else:
			self.row_occurrences[newrow] = (1, self.ptr)
		self.ptr = (self.ptr + 1) % self.size

	def last_seen(self, row):
		if row in self.row_occurrences:
			return self.row_occurrences[row][1]
		else:
			return None

def pack_image(history, img):
	assert(img.width % 2 == 0)
	assert(img.mode == "RGB")
	bimg = img.tobytes()
	w = img.width
	for y in range(img.height):
		raw_row = tuple(bimg[3 * (x + w * y)] >= 0x80 for x in range(w))
		last_seen = history.last_seen(raw_row)
		history.update(raw_row)
		# Uncomment for row dictionary format:
		# if last_seen is None:
		# 	packed = pack_row(raw_row)
		# 	yield struct.pack("<H", len(packed))
		# 	yield packed
		# else:
		# 	yield struct.pack("<H", 0x8000 | last_seen)
		packed = pack_row(raw_row)
		yield bytes((len(packed),))
		yield packed

if __name__ == "__main__":
	filelist = sorted(os.listdir(sys.argv[1]))
	assert(all(x.lower().endswith(".png") for x in filelist))
	ofile = open(sys.argv[2], "wb")
	history = RowHistory(log_size=15)
	for fname in filelist:
		print(fname)
		img = Image.open(os.path.join(sys.argv[1], fname))
		ofile.write(b"".join(pack_image(history, img)))
