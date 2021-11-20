#ifndef _SPRITE_ASM_CONST
#define _SPRITE_ASM_CONST

// Put every function in its own ELF section, to permit linker GC
.macro decl_func name
.section .time_critical.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm

// Assume RGAB5515 (so alpha is bit 5)
// Note the alpha bit being in the same position as RAGB2132 is a coincidence.
// We are just stealing an LSB such that we can treat our alpha pixels in the
// same way as non-alpha pixels when encoding (and the co-opted channel LSB
// always ends up being set on alpha pixels, which is pretty harmless)

#define ALPHA_SHIFT_16BPP 6

// Assume RAGB2132 (so alpha is bit 5)

#define ALPHA_SHIFT_8BPP 6


#endif
