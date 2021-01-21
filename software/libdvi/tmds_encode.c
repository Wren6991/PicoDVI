#include "hardware/interp.h"
#include "tmds_encode.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

static const uint32_t __scratch_x("tmds_table") tmds_table[] = {
#include "tmds_table.h"
};

uint32_t __scratch_x("tmds_table_fullres") tmds_table_fullres_x[] = {
#include "tmds_table_fullres.h"
};

uint32_t __scratch_y("tmds_table_fullres") tmds_table_fullres_y[] = {
#include "tmds_table_fullres.h"
};

// Configure an interpolator to extract a single colour channel from each of a pair
// of pixels, with the first pixel's lsb at pixel_lsb, and the pixels being
// pixel_width wide. Produce a LUT address for the first pixel's colour data on
// LANE0, and the second pixel's colour data on LANE1.
//
// Returns nonzero if the *_leftshift variant of the encoder loop must be used
// (needed for blue channel because I was a stubborn idiot and didn't put
// signed/bidirectional shift on interpolator, very slightly slower). The
// return value is the size of left shift required.

static int configure_interp_for_addrgen(interp_hw_t *interp, uint channel_msb, uint channel_lsb, uint pixel_lsb, uint pixel_width, uint lut_index_width, const uint32_t *lutbase) {
	interp_config c;
	const uint index_shift = 3; // scaled lookup for 8-byte LUT entries

	int shift_channel_to_index = pixel_lsb + channel_msb - (lut_index_width - 1) - index_shift;
	int oops = 0;
	if (shift_channel_to_index < 0) {
		// "It's ok we'll fix it in software"
		oops = -shift_channel_to_index;
		shift_channel_to_index = 0;
	}

	uint index_msb = index_shift + lut_index_width - 1;

	c = interp_default_config();
	interp_config_set_shift(&c, shift_channel_to_index);
	interp_config_set_mask(&c, index_msb - (channel_msb - channel_lsb), index_msb);
	interp_set_config(interp, 0, &c);

	c = interp_default_config();
	interp_config_set_shift(&c, pixel_width	+ shift_channel_to_index);
	interp_config_set_mask(&c, index_msb - (channel_msb - channel_lsb), index_msb);
	interp_config_set_cross_input(&c, true);
	interp_set_config(interp, 1, &c);

	interp->base[0] = (uint32_t)lutbase;
	interp->base[1] = (uint32_t)lutbase;

	return oops;
}

// Extract up to 6 bits from a buffer of 16 bit pixels, and produce a buffer
// of TMDS symbols from this colour channel. Number of pixels must be even,
// pixel buffer must be word-aligned.

void tmds_encode_data_channel_16bpp(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint channel_msb, uint channel_lsb) {
	interp_hw_save_t interp0_save;
	gpio_put(DEBUG_PIN0 + 1, 1);
	interp_save(interp0_hw, &interp0_save);
	int require_lshift = configure_interp_for_addrgen(interp0_hw, channel_msb, channel_lsb, 0, 16, 6, tmds_table);
	if (require_lshift)
		tmds_encode_loop_16bpp_leftshift(pixbuf, symbuf, n_pix, require_lshift);
	else
		tmds_encode_loop_16bpp(pixbuf, symbuf, n_pix);
	interp_restore(interp0_hw, &interp0_save);
	gpio_put(DEBUG_PIN0 + 1, 0);
}

// As above, but 8 bits per pixel, multiple of 4 pixels, and still word-aligned.
void tmds_encode_data_channel_8bpp(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint channel_msb, uint channel_lsb) {
	interp_hw_save_t interp0_save, interp1_save;
	interp_save(interp0_hw, &interp0_save);
	interp_save(interp1_hw, &interp1_save);
	// Note that for 8bpp, some left shift is always required for pixel 0 (any
	// channel), which destroys some MSBs of pixel 3. To get around this, pixel
	// data sent to interp1 is *not left-shifted*
	int require_lshift = configure_interp_for_addrgen(interp0_hw, channel_msb, channel_lsb, 0, 8, 6, tmds_table);
	int lshift_upper = configure_interp_for_addrgen(interp1_hw, channel_msb, channel_lsb, 16, 8, 6, tmds_table);
	assert(!lshift_upper); (void)lshift_upper;
	if (require_lshift)	
		tmds_encode_loop_8bpp_leftshift(pixbuf, symbuf, n_pix, require_lshift);
	else
		tmds_encode_loop_8bpp(pixbuf, symbuf, n_pix);
	interp_restore(interp0_hw, &interp0_save);
	interp_restore(interp1_hw, &interp1_save);
}

// ----------------------------------------------------------------------------
// Code for full-resolution TMDS encode (barely possible, utterly impractical):

// Different scheme used for full res as the fun pixel-doubling DC balance
// trick doesn't work, so we need to actually do running disparity. ACCUM0 has
// pixel data, ACCUM1 has running disparity. INTERP0 is used to process even
// pixels, and INTERP1 for odd pixels. Note this means that even and odd
// symbols have their DC balance handled separately, which is not to spec.

static int configure_interp_for_addrgen_fullres(interp_hw_t *interp, uint channel_msb, uint channel_lsb, uint lut_index_width, const uint32_t *lutbase) {
	const uint index_shift = 2; // scaled lookup for 4-byte LUT entries

	int shift_channel_to_index = channel_msb - (lut_index_width - 1) - index_shift;
	int oops = 0;
	if (shift_channel_to_index < 0) {
		// "It's ok we'll fix it in software"
		oops = -shift_channel_to_index;
		shift_channel_to_index = 0;
	}

	uint index_msb = index_shift + lut_index_width - 1;

	interp_config c;
	// Shift and mask colour channel to lower 6 bits of LUT index (note lut_index_width excludes disparity sign)
	c = interp_default_config();
	interp_config_set_shift(&c, shift_channel_to_index);
	interp_config_set_mask(&c, index_msb - (channel_msb - channel_lsb), index_msb);
	interp_set_config(interp, 0, &c);

	// Concatenate disparity (ACCUM1) sign onto the LUT index
	c = interp_default_config();
	interp_config_set_shift(&c, 30 - index_msb);
	interp_config_set_mask(&c, index_msb + 1, index_msb + 1);
	interp_set_config(interp, 1, &c);

	interp->base[2] = (uint32_t)lutbase;

	return oops;
}

void tmds_encode_data_channel_fullres_16bpp(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint channel_msb, uint channel_lsb) {
	uint core = get_core_num();
	gpio_put(DEBUG_PIN0 + 1 + core, 1);
#ifndef TMDS_FULLRES_NO_INTERP_SAVE
	interp_hw_save_t interp0_save, interp1_save;
	interp_save(interp0_hw, &interp0_save);
	interp_save(interp1_hw, &interp1_save);
#endif

	// There is a copy of the inner loop and the LUT in both scratch X and
	// scratch Y memories. Use X on core 1 and Y on core 0 so the cores don't
	// tread on each other's toes too much.
	const uint32_t *lutbase = core ? tmds_table_fullres_x : tmds_table_fullres_y;
	int lshift_lower = configure_interp_for_addrgen_fullres(interp0_hw, channel_msb, channel_lsb, 6, lutbase);
	int lshift_upper = configure_interp_for_addrgen_fullres(interp1_hw, channel_msb + 16, channel_lsb + 16, 6, lutbase);
	assert(!lshift_upper); (void)lshift_upper;
	if (lshift_lower) {
		(core ?
			tmds_fullres_encode_loop_16bpp_leftshift_x :
			tmds_fullres_encode_loop_16bpp_leftshift_y
		)(pixbuf, symbuf, n_pix, lshift_lower);
	}
	else {
		(core ?
			tmds_fullres_encode_loop_16bpp_x :
			tmds_fullres_encode_loop_16bpp_y
		)(pixbuf, symbuf, n_pix);
	}
#ifndef TMDS_FULLRES_NO_INTERP_SAVE
	interp_restore(interp0_hw, &interp0_save);
	interp_restore(interp1_hw, &interp1_save);
#endif
	gpio_put(DEBUG_PIN0 + 1 + core, 0);
}
