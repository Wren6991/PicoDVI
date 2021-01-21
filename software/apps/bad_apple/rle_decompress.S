// Taking the encoder from DVI spec, with initial balance 0:
// 
// - Encoding either 0x00 or 0xff will produce a running balance of -8, with
//   output symbol of 0x100 or 0x200
// 
// - Subsequently encoding either 0x01 or 0xfe will return the balance to 0, with
//  output symbol of 0x1ff or 0x2ff
// 
// So we can transform a black and white image to TMDS symbols with the
// following table:
// 
// x % 2 | Colour | Output
// ------+--------+--------
// 0     | 0      | 0x9aaaa
// 0     | 1      | 0x6aaaa
// 1     | 0      | 0x95555
// 1     | 1      | 0x65555
// 
// We will use this to rapidly unpack bytes encoded in the following RLE:
// 
// 7|6|5    0
// a|b|nnnnnn
// 
// where a, b are the colours of a two-pixel pair, and n is a repeat
// count between 1 and 64. 

// r0: Input buffer (byte-aligned)
// r1: Output buffer (word-aligned)
// r2: Input size (bytes)
.global rle_to_tmds
.type rle_to_tmds,%function
.thumb_func
rle_to_tmds:
	push {r4-r6, lr}
	add r2, r0
	mov ip, r2
	adr r5, tmds_bw_pix0
	adr r6, tmds_bw_pix1
	b 3f
1:
	ldrb r2, [r0]
	add r0, #1

	// Pixel 0 symbol in r3: (could speed these bits up with interp)
	lsr r3, r2, #7
	lsl r3, #2
	ldr r3, [r3, r5]

	// Pixel 1 symbol in r4:
	lsr r4, r2, #6
	lsl r4, #31
	lsr r4, #29
	ldr r4, [r4, r6]

	// run length - 1 in r2:
	lsl r2, #26
	lsr r2, #26
2:
	stmia r1!, {r3, r4}
	sub r2, #1
	bcs 2b
3:
	cmp r0, ip
	bne 1b
	pop {r4-r6, pc}

.align 2
tmds_bw_pix0:
	.word 0x9aaaa
	.word 0x6aaaa
tmds_bw_pix1:
	.word 0x95555
	.word 0x65555