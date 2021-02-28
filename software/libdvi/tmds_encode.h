#ifndef _TMDS_ENCODE_H_
#define _TMDS_ENCODE_H_

#include "hardware/interp.h"
#include "dvi_config_defs.h"

// Functions from tmds_encode.c
void tmds_encode_data_channel_16bpp(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint channel_msb, uint channel_lsb);
void tmds_encode_data_channel_8bpp(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint channel_msb, uint channel_lsb);
void tmds_encode_data_channel_fullres_16bpp(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint channel_msb, uint channel_lsb);

// Functions from tmds_encode.S

void tmds_encode_1bpp(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix);

// Uses interp0:
void tmds_encode_loop_16bpp(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix);
void tmds_encode_loop_16bpp_leftshift(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint leftshift);

// Uses interp0 and interp1:
void tmds_encode_loop_8bpp(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix);
void tmds_encode_loop_8bpp_leftshift(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint leftshift);

// Uses interp0 and interp1:
// (Note a copy is provided in scratch memories X and Y)
void tmds_fullres_encode_loop_16bpp_x(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix);
void tmds_fullres_encode_loop_16bpp_y(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix);
void tmds_fullres_encode_loop_16bpp_leftshift_x(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint leftshift);
void tmds_fullres_encode_loop_16bpp_leftshift_y(const uint32_t *pixbuf, uint32_t *symbuf, size_t n_pix, uint leftshift);

#endif
