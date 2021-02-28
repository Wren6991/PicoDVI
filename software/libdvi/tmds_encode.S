#include "hardware/regs/addressmap.h"
#include "hardware/regs/sio.h"
#include "dvi_config_defs.h"

// Offsets suitable for ldr/str (must be <= 0x7c):
#define ACCUM0_OFFS     (SIO_INTERP0_ACCUM0_OFFSET     - SIO_INTERP0_ACCUM0_OFFSET)
#define ACCUM1_OFFS     (SIO_INTERP0_ACCUM1_OFFSET     - SIO_INTERP0_ACCUM0_OFFSET)
#define ACCUM1_ADD_OFFS (SIO_INTERP0_ACCUM1_ADD_OFFSET - SIO_INTERP0_ACCUM0_OFFSET)
#define PEEK0_OFFS      (SIO_INTERP0_PEEK_LANE0_OFFSET - SIO_INTERP0_ACCUM0_OFFSET)
#define PEEK1_OFFS      (SIO_INTERP0_PEEK_LANE1_OFFSET - SIO_INTERP0_ACCUM0_OFFSET)
#define PEEK2_OFFS      (SIO_INTERP0_PEEK_FULL_OFFSET  - SIO_INTERP0_ACCUM0_OFFSET)
#define INTERP1         (SIO_INTERP1_ACCUM0_OFFSET     - SIO_INTERP0_ACCUM0_OFFSET)
// Note the entirety of INTERP0 and INTERP1 fits inside this 5-bit
// word-addressed space... almost as though it were intentional! :)

.syntax unified
.cpu cortex-m0plus
.thumb

.macro decl_func_x name
.section .scratch_x.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm

.macro decl_func_y name
.section .scratch_y.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm

#define decl_func decl_func_x

// ----------------------------------------------------------------------------
// Pixel-doubling encoders for RGB

// r0: Input buffer (word-aligned)
// r1: Output buffer (word-aligned)
// r2: Input size (pixels)

decl_func tmds_encode_loop_16bpp
	push {r4, r5, r6, r7, lr}
	lsls r2, #2
	add r2, r1
	mov ip, r2
	ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)
	b 2f
.align 2
1:
.rept TMDS_ENCODE_UNROLL
	ldmia r0!, {r4}
	str r4, [r2, #ACCUM0_OFFS]
	ldr r4, [r2, #PEEK0_OFFS]
	ldr r4, [r4]
	ldr r6, [r2, #PEEK1_OFFS]
	ldr r6, [r6]
	// TODO our pixels are now 2 per word instead of 1 per word, so this store is
	// now 2 words instead of 4; reexpand it.
	stmia r1!, {r4, r6}
.endr
2:
	cmp r1, ip
	bne 1b
	pop {r4, r5, r6, r7, pc}

// Same as above, but scale data to make up for lack of left shift
// in interpolator (costs 1 cycle per 2 pixels)
//
// r0: Input buffer (word-aligned)
// r1: Output buffer (word-aligned)
// r2: Input size (pixels)
// r3: Left shift amount

decl_func tmds_encode_loop_16bpp_leftshift
	push {r4, r5, r6, r7, lr}
	lsls r2, #2
	add r2, r1
	mov ip, r2
	ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)
	b 2f
.align 2
1:
.rept TMDS_ENCODE_UNROLL
	ldmia r0!, {r4}
	lsls r4, r3
	str r4, [r2, #ACCUM0_OFFS]
	ldr r4, [r2, #PEEK0_OFFS]
	ldr r4, [r4]
	ldr r6, [r2, #PEEK1_OFFS]
	ldr r6, [r6]
	stmia r1!, {r4, r6}
.endr
2:
	cmp r1, ip
	bne 1b
	pop {r4, r5, r6, r7, pc}

// r0: Input buffer (word-aligned)
// r1: Output buffer (word-aligned)
// r2: Input size (pixels)

decl_func tmds_encode_loop_8bpp
	push {r4, r5, r6, r7, lr}
	lsls r2, #2
	add r2, r1
	mov ip, r2
	ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)
	b 2f
.align 2
1:
.rept TMDS_ENCODE_UNROLL
	ldmia  r0!, {r4}
	str r4, [r2, #ACCUM0_OFFS + INTERP1]
	str r4, [r2, #ACCUM0_OFFS]
	ldr r4, [r2, #PEEK0_OFFS]
	ldr r4, [r4]
	ldr r5, [r2, #PEEK1_OFFS]
	ldr r5, [r5]
	ldr r6, [r2, #PEEK0_OFFS + INTERP1]
	ldr r6, [r6]
	ldr r7, [r2, #PEEK1_OFFS + INTERP1]
	ldr r7, [r7]
	stmia r1!, {r4, r5, r6, r7}
.endr
2:
	cmp r1, ip
	bne 1b
	pop {r4, r5, r6, r7, pc}

// r0: Input buffer (word-aligned)
// r1: Output buffer (word-aligned)
// r2: Input size (pixels)
// r3: Left shift amount
//
// Note that only the data written to interp0 (pixel 0, 1) is leftshifted, not
// the data written to interp1 (pixel 2, 3). Otherwise we always lose MSBs, as
// the LUT offset MSB is at bit 8, so pixel 0 always requires some left shift,
// since its channel MSBs are no greater than 7.

decl_func tmds_encode_loop_8bpp_leftshift
	push {r4, r5, r6, r7, lr}
	lsls r2, #3
	add r2, r1
	mov ip, r2
	ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)
	b 2f
.align 2
1:
.rept TMDS_ENCODE_UNROLL
	ldmia  r0!, {r4}
	str r4, [r2, #ACCUM0_OFFS + INTERP1]
	lsls r4, r3
	str r4, [r2, #ACCUM0_OFFS]
	ldr r4, [r2, #PEEK0_OFFS]
	ldr r4, [r4]
	ldr r5, [r2, #PEEK1_OFFS]
	ldr r5, [r5]
	ldr r6, [r2, #PEEK0_OFFS + INTERP1]
	ldr r6, [r6]
	ldr r7, [r2, #PEEK1_OFFS + INTERP1]
	ldr r7, [r7]
	stmia r1!, {r4, r5, r6, r7}
.endr
2:
	cmp r1, ip
	bne 1b
	pop {r4, r5, r6, r7, pc}

// ----------------------------------------------------------------------------
// Fast 1bpp black/white encoder (full res)

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


// Encode 4 pixels @ 1bpp (using one table lookup)
// r3 contains lookup mask (preshifted)
// r8 contains pointer to encode table
// 3.25 cyc/pix
.macro tmds_encode_1bpp_body shift_instr shamt
	\shift_instr r4, r2, #\shamt
	ands r4, r3
	add r4, r8
	ldmia r4, {r4-r7}
	stmia r1!, {r4-r7}
.endm

// r0: input buffer (word-aligned)
// r1: output buffer (word-aligned)
// r2: output pixel count
decl_func tmds_encode_1bpp
	push {r4-r7, lr}
	mov r7, r8
	push {r7}
	lsls r2, #2
	add r2, r1
	mov ip, r2
	adr r4, tmds_1bpp_table
	mov r8, r4
	// Mask: 4 bit index, 16 bytes per entry
	movs r3, #0xf0
	b 2f
1:
	ldmia r0!, {r2}
	tmds_encode_1bpp_body lsls 4
	tmds_encode_1bpp_body lsls 0
	tmds_encode_1bpp_body lsrs 4
	tmds_encode_1bpp_body lsrs 8
	tmds_encode_1bpp_body lsrs 12
	tmds_encode_1bpp_body lsrs 16
	tmds_encode_1bpp_body lsrs 20
	tmds_encode_1bpp_body lsrs 24
2:
	cmp r1, ip
	blo 1b

	pop {r7}
	mov r8, r7
	pop {r4-r7, pc}

.align 2
tmds_1bpp_table:
	.word 0x9aaaa, 0x95555, 0x9aaaa, 0x95555   // 0000
	.word 0x6aaaa, 0x95555, 0x9aaaa, 0x95555   // 0001
	.word 0x9aaaa, 0x65555, 0x9aaaa, 0x95555   // 0010
	.word 0x6aaaa, 0x65555, 0x9aaaa, 0x95555   // 0011
	.word 0x9aaaa, 0x95555, 0x6aaaa, 0x95555   // 0100
	.word 0x6aaaa, 0x95555, 0x6aaaa, 0x95555   // 0101
	.word 0x9aaaa, 0x65555, 0x6aaaa, 0x95555   // 0110
	.word 0x6aaaa, 0x65555, 0x6aaaa, 0x95555   // 0111
	.word 0x9aaaa, 0x95555, 0x9aaaa, 0x65555   // 1000
	.word 0x6aaaa, 0x95555, 0x9aaaa, 0x65555   // 1001
	.word 0x9aaaa, 0x65555, 0x9aaaa, 0x65555   // 1010
	.word 0x6aaaa, 0x65555, 0x9aaaa, 0x65555   // 1011
	.word 0x9aaaa, 0x95555, 0x6aaaa, 0x65555   // 1100
	.word 0x6aaaa, 0x95555, 0x6aaaa, 0x65555   // 1101
	.word 0x9aaaa, 0x65555, 0x6aaaa, 0x65555   // 1110
	.word 0x6aaaa, 0x65555, 0x6aaaa, 0x65555   // 1111

// ----------------------------------------------------------------------------
// Full-resolution RGB encode (not very practical)

// Non-doubled TMDS encode. 8.333 cycles per pixel, no exceptions. (This is
// taking horizontal blanking (at VGA) and dual core into account, and
// assuming the 3 channels are encoded individually.)
//
// Here is an idea
// Have a table with a 7 bit lookup. The lookup is the 6 colour data bits (in
// ACCUM0), concatenated with the sign bit of our running disparity (from
// ACCUM1). Each table entry is a 20-bit TMDS symbol (pseudodifferential),
// with the symbol's disparity stored left-justified in the upper 12 bits, as
// e.g. a 6 bit signed integer.
//
// - Load pixel data.                        cyc: 0.75 (ldmia 2 words, every 4 pixels)
// - Write pixel to ACCUM0.                  cyc: 1
// - Read address from PEEK2.                cyc: 1
// - Load encoded pixel from address.        cyc: 2
// - Write disparity data to ACCUM1_ADD      cyc: 1
// - Write encoded data to output buffer.    cyc: 1.25 (stmia 4 words, every 4 pixels)
//
// With decent register allocation we may be able to load 4 pixels at
// once (2 words), and write 4 at once (4 words). This gives 7 cyc/pix.
//
// One issue is that the TMDS data in the bottom of ACCUM1 will eventually
// overflow and affect the running disparity. At least 64 pixels will pass
// before this happens, so we just need to read out ACCUM1, mask it, and write
// it back at least once every 64 pixels. This could be done at the end of
// every unrolled loop, before the test and branch. At 16 pix/loop this is an
// additional 0.5 cyc/pix.
//
// Note that we need to use two interpolators to get the bits from both pixels
// -- we are not outputting a single DC-balanced stream, but rather two
// interleaved streams which are each DC-balanced. This is fine electrically,
// but our output here will *NOT* match the TMDS encoder given in the DVI
// spec.

// You can define TMDS_FULLRES_NO_DC_BALANCE to disable the running balance
// feedback. With the feedback enabled (default), the output is DC balanced,
// but there are just barely enough CPU cycles to do all the encode, so it's
// essentially a party trick. If you disable DC balancing, the performance is
// much better, and many monitors will still accept the signals as long as you
// DC couple your DVI signals.

// r0: Input buffer (word-aligned)
// r1: Output buffer (word-aligned)
// r2: Pixel count

.macro tmds_fullres_encode_loop_16bpp
	push {r4-r7, lr}
	mov r4, r8
	push {r4}


	lsls r2, #2
	add r2, r1
	mov ip, r2
	ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)
	// DC balance defined to be 0 at start of scanline:
	movs r4, #0
	str r4, [r2, #ACCUM1_OFFS]
	str r4, [r2, #ACCUM1_OFFS + INTERP1]

	// Mask for upper 6 bits of accum in r3, used to break carries from TMDS
	// symbols into DC balance in ACCUM1
	movs r3, #0x3f
	lsls r3, #26

	// Keep loop start pointer in r8 so we can get a longer backward branch
	adr r4, 1f
	adds r4, #1 // god damn thumb bit why is this a thing
	mov r8, r4
	b 2f
	.align 2
1:
.rept 16
	ldmia r0!, {r4, r6}
	str r4, [r2, #ACCUM0_OFFS + INTERP1]
	str r4, [r2, #ACCUM0_OFFS]
	ldr r4, [r2, #PEEK2_OFFS]
	ldr r4, [r4]
#if !TMDS_FULLRES_NO_DC_BALANCE
	str r4, [r2, #ACCUM1_ADD_OFFS]
#endif
	ldr r5, [r2, #PEEK2_OFFS + INTERP1]
	ldr r5, [r5]
#if !TMDS_FULLRES_NO_DC_BALANCE
	str r5, [r2, #ACCUM1_ADD_OFFS + INTERP1]
#endif

	str r6, [r2, #ACCUM0_OFFS + INTERP1]
	str r6, [r2, #ACCUM0_OFFS]
	ldr r6, [r2, #PEEK2_OFFS]
	ldr r6, [r6]
#if !TMDS_FULLRES_NO_DC_BALANCE
	str r6, [r2, #ACCUM1_ADD_OFFS]
#endif
	ldr r7, [r2, #PEEK2_OFFS + INTERP1]
	ldr r7, [r7]
#if !TMDS_FULLRES_NO_DC_BALANCE
	str r7, [r2, #ACCUM1_ADD_OFFS + INTERP1]
#endif

	stmia r1!, {r4, r5, r6, r7}
.endr
	// Need to mask away bottoms of ACCUM1 before the TMDS data overflows into
	// disparity counter. This costs 8 cycles per loop body.
#if !TMDS_FULLRES_NO_DC_BALANCE
	ldr r4, [r2, #ACCUM1_OFFS]
	ands r4, r3
	str r4, [r2, #ACCUM1_OFFS]
	ldr r4, [r2, #ACCUM1_OFFS + INTERP1]
	ands r4, r3
	str r4, [r2, #ACCUM1_OFFS + INTERP1]
#endif
2:
	cmp r1, ip
	beq 1f
	bx r8
1:
	pop {r4}
	mov r8, r4
	pop {r4-r7, pc}
.endm

// One copy each in X and Y, so the two cores don't step on each other
decl_func_x tmds_fullres_encode_loop_16bpp_x
	tmds_fullres_encode_loop_16bpp
decl_func_y tmds_fullres_encode_loop_16bpp_y
	tmds_fullres_encode_loop_16bpp


// r0: Input buffer (word-aligned)
// r1: Output buffer (word-aligned)
// r2: Pixel count
// r3: Left shift amount

.macro tmds_fullres_encode_loop_16bpp_leftshift
	push {r4-r7, lr}
	mov r4, r8
	mov r5, r9
	push {r4-r5}

	lsls r2, #2
	add r2, r1
	mov ip, r2
	ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)
	// DC balance defined to be 0 at start of scanline:
	movs r4, #0
	str r4, [r2, #ACCUM1_OFFS]
	str r4, [r2, #ACCUM1_OFFS + INTERP1]

	// Mask for upper 6 bits of accum in r9, used to break carries from TMDS
	// symbols into DC balance in ACCUM1
	movs r4, #0x3f
	lsls r4, #26
	mov r9, r4

	adr r4, 1f
	adds r4, #1
	mov r8, r4
	b 2f
	.align 2
1:
.rept 16 // 64 pixels per iteration
	ldmia r0!, {r4, r6}
	// Note we apply the leftshift for INTERP0 only
	str r4, [r2, #ACCUM0_OFFS + INTERP1]
	lsls r4, r3
	str r4, [r2, #ACCUM0_OFFS]
	ldr r4, [r2, #PEEK2_OFFS]
	ldr r4, [r4]
#if !TMDS_FULLRES_NO_DC_BALANCE
	str r4, [r2, #ACCUM1_ADD_OFFS]
#endif
	ldr r5, [r2, #PEEK2_OFFS + INTERP1]
	ldr r5, [r5]
#if !TMDS_FULLRES_NO_DC_BALANCE
	str r5, [r2, #ACCUM1_ADD_OFFS + INTERP1]
#endif

	str r6, [r2, #ACCUM0_OFFS + INTERP1]
	lsls r6, r3
	str r6, [r2, #ACCUM0_OFFS]
	ldr r6, [r2, #PEEK2_OFFS]
	ldr r6, [r6]
#if !TMDS_FULLRES_NO_DC_BALANCE
	str r6, [r2, #ACCUM1_ADD_OFFS]
#endif
	ldr r7, [r2, #PEEK2_OFFS + INTERP1]
	ldr r7, [r7]
#if !TMDS_FULLRES_NO_DC_BALANCE
	str r7, [r2, #ACCUM1_ADD_OFFS + INTERP1]
#endif

	stmia r1!, {r4, r5, r6, r7}
.endr
	// Need to mask away bottoms of ACCUM1 before the TMDS data overflows into
	// disparity counter
#if !TMDS_FULLRES_NO_DC_BALANCE
	mov r5, r9
	ldr r4, [r2, #ACCUM1_OFFS]
	ands r4, r5
	str r4, [r2, #ACCUM1_OFFS]
	ldr r4, [r2, #ACCUM1_OFFS + INTERP1]
	ands r4, r5
	str r4, [r2, #ACCUM1_OFFS + INTERP1]
#endif
2:
	cmp r1, ip
	beq 1f
	bx r8
1:
	pop {r4-r5}
	mov r8, r4
	mov r9, r5
	pop {r4-r7, pc}
.endm

decl_func_x tmds_fullres_encode_loop_16bpp_leftshift_x
	tmds_fullres_encode_loop_16bpp_leftshift
decl_func_y tmds_fullres_encode_loop_16bpp_leftshift_y
	tmds_fullres_encode_loop_16bpp_leftshift
