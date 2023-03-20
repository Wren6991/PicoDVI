#include <stdlib.h>
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "dvi.h"
#include "dvi_timing.h"
#include "dvi_serialiser.h"
#include "tmds_encode.h"

// Time-critical functions pulled into RAM but each in a unique section to
// allow garbage collection
#define __dvi_func(f) __not_in_flash_func(f)
#define __dvi_func_x(f) __scratch_x(__STRING(f)) f

// We require exclusive use of a DMA IRQ line. (you wouldn't want to share
// anyway). It's possible in theory to hook both IRQs and have two DVI outs.
static struct dvi_inst *dma_irq_privdata[2];
static void dvi_dma0_irq();
static void dvi_dma1_irq();

void dvi_init(struct dvi_inst *inst, uint spinlock_tmds_queue, uint spinlock_colour_queue) {
    inst->dvi_started = false;
    inst->timing_state.v_ctr  = 0;
    inst->dvi_frame_count = 0;
    
    dvi_audio_init(inst);
    dvi_timing_state_init(&inst->timing_state);
    dvi_serialiser_init(&inst->ser_cfg);
    for (int i = 0; i < N_TMDS_LANES; ++i) {
        inst->dma_cfg[i].chan_ctrl = dma_claim_unused_channel(true);
        inst->dma_cfg[i].chan_data = dma_claim_unused_channel(true);
        inst->dma_cfg[i].tx_fifo = (void*)&inst->ser_cfg.pio->txf[inst->ser_cfg.sm_tmds[i]];
        inst->dma_cfg[i].dreq = pio_get_dreq(inst->ser_cfg.pio, inst->ser_cfg.sm_tmds[i], true);
    }
    inst->late_scanline_ctr = 0;
    inst->tmds_buf_release[0] = NULL;
    inst->tmds_buf_release[1] = NULL;
    queue_init_with_spinlock(&inst->q_tmds_valid,   sizeof(void*),  8, spinlock_tmds_queue);
    queue_init_with_spinlock(&inst->q_tmds_free,    sizeof(void*),  8, spinlock_tmds_queue);
    queue_init_with_spinlock(&inst->q_colour_valid, sizeof(void*),  8, spinlock_colour_queue);
    queue_init_with_spinlock(&inst->q_colour_free,  sizeof(void*),  8, spinlock_colour_queue);

    dvi_setup_scanline_for_vblank(inst->timing, inst->dma_cfg, true, &inst->dma_list_vblank_sync);
    dvi_setup_scanline_for_vblank(inst->timing, inst->dma_cfg, false, &inst->dma_list_vblank_nosync);
    dvi_setup_scanline_for_active(inst->timing, inst->dma_cfg, (void*)SRAM_BASE, &inst->dma_list_active, false);
    dvi_setup_scanline_for_active(inst->timing, inst->dma_cfg, NULL, &inst->dma_list_error, false);
    dvi_setup_scanline_for_active(inst->timing, inst->dma_cfg, NULL, &inst->dma_list_active_blank, true);

    for (int i = 0; i < DVI_N_TMDS_BUFFERS; ++i) {
#if DVI_MONOCHROME_TMDS
        void *tmdsbuf = malloc(inst->timing->h_active_pixels / DVI_SYMBOLS_PER_WORD * sizeof(uint32_t));
#else
        void *tmdsbuf = malloc(TMDS_CHANNELS * inst->timing->h_active_pixels / DVI_SYMBOLS_PER_WORD * sizeof(uint32_t));
#endif
        if (!tmdsbuf) {
            panic("TMDS buffer allocation failed");
        }
        queue_add_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
    }

    set_AVI_info_frame(&inst->avi_info_frame, UNDERSCAN, RGB, ITU601, PIC_ASPECT_RATIO_4_3, SAME_AS_PAR, FULL, _640x480P60);

}

// The IRQs will run on whichever core calls this function (this is why it's
// called separately from dvi_init)
void dvi_register_irqs_this_core(struct dvi_inst *inst, uint irq_num) {
    uint32_t mask_sync_channel = 1u << inst->dma_cfg[TMDS_SYNC_LANE].chan_data;
    uint32_t mask_all_channels = 0;
    for (int i = 0; i < N_TMDS_LANES; ++i)
        mask_all_channels |= 1u << inst->dma_cfg[i].chan_ctrl | 1u << inst->dma_cfg[i].chan_data;

    dma_hw->ints0 = mask_sync_channel;
    if (irq_num == DMA_IRQ_0) {
        hw_write_masked(&dma_hw->inte0, mask_sync_channel, mask_all_channels);
        dma_irq_privdata[0] = inst;
        irq_set_exclusive_handler(DMA_IRQ_0, dvi_dma0_irq);
    }
    else {
        hw_write_masked(&dma_hw->inte1, mask_sync_channel, mask_all_channels);
        dma_irq_privdata[1] = inst;
        irq_set_exclusive_handler(DMA_IRQ_1, dvi_dma1_irq);
    }
    irq_set_enabled(irq_num, true);
}

void dvi_unregister_irqs_this_core(struct dvi_inst *inst, uint irq_num) {
    irq_set_enabled(irq_num, false);
    if (irq_num == DMA_IRQ_0) {
         irq_remove_handler(DMA_IRQ_0, dvi_dma0_irq);
    } else {
         irq_remove_handler(DMA_IRQ_1, dvi_dma1_irq);
    }
    if (inst->tmds_buf_release[1]) {
        queue_try_add_u32(&inst->q_tmds_free, &inst->tmds_buf_release[1]);
    }
    if (inst->tmds_buf_release[0]) {
        queue_try_add_u32(&inst->q_tmds_free, &inst->tmds_buf_release[0]);
    }
    inst->tmds_buf_release[1] = NULL;
    inst->tmds_buf_release[0] = NULL;
}

// Set up control channels to make transfers to data channels' control
// registers (but don't trigger the control channels -- this is done either by
// data channel CHAIN_TO or an initial write to MULTI_CHAN_TRIGGER)
static inline void __attribute__((always_inline)) _dvi_load_dma_op(const struct dvi_lane_dma_cfg dma_cfg[], struct dvi_scanline_dma_list *l) {
    for (int i = 0; i < N_TMDS_LANES; ++i) {
        dma_channel_config cfg = dma_channel_get_default_config(dma_cfg[i].chan_ctrl);
        channel_config_set_ring(&cfg, true, 4); // 16-byte write wrap
        channel_config_set_read_increment(&cfg, true);
        channel_config_set_write_increment(&cfg, true);
        dma_channel_configure(
            dma_cfg[i].chan_ctrl,
            &cfg,
            &dma_hw->ch[dma_cfg[i].chan_data],
            dvi_lane_from_list(l, i),
            4, // Configure all 4 registers then halt until next CHAIN_TO
            false
        );
    }
}

// Setup first set of control block lists, configure the control channels, and
// trigger them. Control channels will subsequently be triggered only by DMA
// CHAIN_TO on data channel completion. IRQ handler *must* be prepared before
// calling this. (Hooked to DMA IRQ0)
void dvi_start(struct dvi_inst *inst) {
    if (inst->dvi_started) {
        return;
    }
    _dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_vblank_nosync);
    dma_start_channel_mask(
        (1u << inst->dma_cfg[0].chan_ctrl) |
        (1u << inst->dma_cfg[1].chan_ctrl) |
        (1u << inst->dma_cfg[2].chan_ctrl));

    // We really don't want the FIFOs to bottom out, so wait for full before
    // starting the shift-out.
    for (int i = 0; i < N_TMDS_LANES; ++i)
        while (!pio_sm_is_tx_fifo_full(inst->ser_cfg.pio, inst->ser_cfg.sm_tmds[i]))
            tight_loop_contents();
    dvi_serialiser_enable(&inst->ser_cfg, true);
    inst->dvi_started = true;
}

void dvi_stop(struct dvi_inst *inst) {
    if (!inst->dvi_started) {
        return;
    }
    uint mask  = 0;
    for (int i = 0; i < N_TMDS_LANES; ++i) {
        dma_channel_config cfg = dma_channel_get_default_config(inst->dma_cfg[i].chan_ctrl);
        dma_channel_set_config(inst->dma_cfg[i].chan_ctrl, &cfg, false);
        cfg = dma_channel_get_default_config(inst->dma_cfg[i].chan_data);
        dma_channel_set_config(inst->dma_cfg[i].chan_data, &cfg, false);
        mask |= 1 << inst->dma_cfg[i].chan_data;
        mask |= 1 << inst->dma_cfg[i].chan_ctrl;
    }

    dma_channel_abort(mask);
    dma_irqn_acknowledge_channel(0, inst->dma_cfg[TMDS_SYNC_LANE].chan_data);
    dma_hw->ints0 = 1u << inst->dma_cfg[TMDS_SYNC_LANE].chan_data;

    dvi_serialiser_enable(&inst->ser_cfg, false);
    inst->dvi_started = false;
}

static inline void __dvi_func_x(_dvi_prepare_scanline_8bpp)(struct dvi_inst *inst, uint32_t *scanbuf) {
    uint32_t *tmdsbuf = NULL;
    queue_remove_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
    uint pixwidth = inst->timing->h_active_pixels;
    uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;
    // Scanline buffers are half-resolution; the functions take the number of *input* pixels as parameter.
    tmds_encode_data_channel_8bpp(scanbuf, tmdsbuf + 0 * words_per_channel, pixwidth / 2, DVI_8BPP_BLUE_MSB,  DVI_8BPP_BLUE_LSB );
    tmds_encode_data_channel_8bpp(scanbuf, tmdsbuf + 1 * words_per_channel, pixwidth / 2, DVI_8BPP_GREEN_MSB, DVI_8BPP_GREEN_LSB);
    tmds_encode_data_channel_8bpp(scanbuf, tmdsbuf + 2 * words_per_channel, pixwidth / 2, DVI_8BPP_RED_MSB,   DVI_8BPP_RED_LSB  );
    queue_add_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);
}

static inline void __dvi_func_x(_dvi_prepare_scanline_16bpp)(struct dvi_inst *inst, uint32_t *scanbuf) {
    uint32_t *tmdsbuf = NULL;
    queue_remove_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
    uint pixwidth = inst->timing->h_active_pixels;
    uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;
    tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 0 * words_per_channel, pixwidth / 2, DVI_16BPP_BLUE_MSB,  DVI_16BPP_BLUE_LSB );
    tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 1 * words_per_channel, pixwidth / 2, DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
    tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 2 * words_per_channel, pixwidth / 2, DVI_16BPP_RED_MSB,   DVI_16BPP_RED_LSB  );
    queue_add_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);
}

// "Worker threads" for TMDS encoding (core enters and never returns, but still handles IRQs)

// Version where each record in q_colour_valid is one scanline:
void __dvi_func(dvi_scanbuf_main_8bpp)(struct dvi_inst *inst) {
    while (1) {
        uint32_t *scanbuf = NULL;
        queue_remove_blocking_u32(&inst->q_colour_valid, &scanbuf);
        _dvi_prepare_scanline_8bpp(inst, scanbuf);
        queue_add_blocking_u32(&inst->q_colour_free, &scanbuf);
    }
    __builtin_unreachable();
}

// Ugh copy/paste but it lets us garbage collect the TMDS stuff that is not being used from .scratch_x
void __dvi_func(dvi_scanbuf_main_16bpp)(struct dvi_inst *inst) {
    while (1) {
        uint32_t *scanbuf = NULL;
        queue_remove_blocking_u32(&inst->q_colour_valid, &scanbuf);
        _dvi_prepare_scanline_16bpp(inst, scanbuf);
        queue_add_blocking_u32(&inst->q_colour_free, &scanbuf);
    }
    __builtin_unreachable();
}

static void __dvi_func(dvi_dma_irq_handler)(struct dvi_inst *inst) {
    // Every fourth interrupt marks the start of the horizontal active region. We
    // now have until the end of this region to generate DMA blocklist for next
    // scanline.
    dvi_timing_state_advance(inst->timing, &inst->timing_state);
    
    // Make sure all three channels have definitely loaded their last block
    // (should be within a few cycles of one another)
    for (int i = 0; i < N_TMDS_LANES; ++i) {
        while (dma_debug_hw->ch[inst->dma_cfg[i].chan_data].tcr != inst->timing->h_active_pixels / DVI_SYMBOLS_PER_WORD) {
            tight_loop_contents();
        }    
    }

    if (inst->tmds_buf_release[1] && !queue_try_add_u32(&inst->q_tmds_free, &inst->tmds_buf_release[1])) {
        panic("TMDS free queue full in IRQ!");
    }
    inst->tmds_buf_release[1] = inst->tmds_buf_release[0];
    inst->tmds_buf_release[0] = NULL;

    uint32_t *tmdsbuf = NULL;
    while (inst->late_scanline_ctr > 0 && queue_try_remove_u32(&inst->q_tmds_valid, &tmdsbuf)) {
        // If we displayed this buffer then it would be in the wrong vertical
        // position on-screen. Just pass it back.
        queue_add_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
        --inst->late_scanline_ctr;
    }

    switch (inst->timing_state.v_state) {
        case DVI_STATE_ACTIVE:
        {
            bool is_blank_line = false;
            if (inst->timing_state.v_ctr < inst->blank_settings.top ||
                inst->timing_state.v_ctr >= (inst->timing->v_active_lines - inst->blank_settings.bottom))
            {
                // Is a Blank Line
                is_blank_line = true;
            }
            else
            {
                if (queue_try_peek_u32(&inst->q_tmds_valid, &tmdsbuf))
                {
                    if (inst->timing_state.v_ctr % DVI_VERTICAL_REPEAT == DVI_VERTICAL_REPEAT - 1)
                    {
                        queue_remove_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);
                        inst->tmds_buf_release[0] = tmdsbuf;
                    }
                }
                else
                {
                    // No valid scanline was ready (generates solid red scanline)
                    tmdsbuf = NULL;
                    if (inst->timing_state.v_ctr % DVI_VERTICAL_REPEAT == DVI_VERTICAL_REPEAT - 1)
                    {
                        ++inst->late_scanline_ctr;
                    }
                }

                if (inst->scanline_is_enabled && (inst->timing_state.v_ctr & 1))
                {
                    is_blank_line = true;
                }
            }

            if (is_blank_line)
            {
                _dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_active_blank);
            }
            else if (tmdsbuf)
            {
                dvi_update_scanline_data_dma(inst->timing, tmdsbuf, &inst->dma_list_active, inst->data_island_is_enabled);
                _dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_active);
            }
            else
            {
                _dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_error);
            }
            if (inst->scanline_callback && inst->timing_state.v_ctr % DVI_VERTICAL_REPEAT == DVI_VERTICAL_REPEAT - 1)
            {
                inst->scanline_callback(inst->timing_state.v_ctr / DVI_VERTICAL_REPEAT);
            }
        }
        break;

        case DVI_STATE_SYNC:
            _dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_vblank_sync);
            if (inst->timing_state.v_ctr == 0) {
                ++inst->dvi_frame_count;
            }
            break;

        default:
            _dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_vblank_nosync);
            break;
    }

    if (inst->data_island_is_enabled) {
        dvi_update_data_packet(inst);
    }
}

static void __dvi_func(dvi_dma0_irq)() {
    struct dvi_inst *inst = dma_irq_privdata[0];
    dma_hw->ints0 = 1u << inst->dma_cfg[TMDS_SYNC_LANE].chan_data;
    dvi_dma_irq_handler(inst);
}

static void __dvi_func(dvi_dma1_irq)() {
    struct dvi_inst *inst = dma_irq_privdata[1];
    dma_hw->ints1 = 1u << inst->dma_cfg[TMDS_SYNC_LANE].chan_data;
    dvi_dma_irq_handler(inst);
}

// DVI Data island related
void dvi_audio_init(struct dvi_inst *inst) {
    inst->data_island_is_enabled = false;
    inst->scanline_is_enabled = false;
    inst->audio_freq = 0;
    inst->samples_per_frame = 0;
    inst->samples_per_line16 = 0;
    inst->left_audio_sample_count = 0;
    inst->audio_sample_pos = 0;
    inst->audio_frame_count = 0;
}

void dvi_enable_data_island(struct dvi_inst *inst) {
    inst->data_island_is_enabled  = true;

    dvi_setup_scanline_for_vblank_with_audio(inst->timing, inst->dma_cfg, true, &inst->dma_list_vblank_sync);
    dvi_setup_scanline_for_vblank_with_audio(inst->timing, inst->dma_cfg, false, &inst->dma_list_vblank_nosync);
    dvi_setup_scanline_for_active_with_audio(inst->timing, inst->dma_cfg, (void*)SRAM_BASE, &inst->dma_list_active, false);
    dvi_setup_scanline_for_active_with_audio(inst->timing, inst->dma_cfg, NULL, &inst->dma_list_error, false);
    dvi_setup_scanline_for_active_with_audio(inst->timing, inst->dma_cfg, NULL, &inst->dma_list_active_blank, true);

    // Setup internal Data Packet streams
    dvi_update_data_island_ptr(&inst->dma_list_vblank_sync,   &inst->next_data_stream);
    dvi_update_data_island_ptr(&inst->dma_list_vblank_nosync, &inst->next_data_stream);
    dvi_update_data_island_ptr(&inst->dma_list_active,        &inst->next_data_stream);
    dvi_update_data_island_ptr(&inst->dma_list_error,         &inst->next_data_stream);
    dvi_update_data_island_ptr(&inst->dma_list_active_blank,  &inst->next_data_stream);
}

void dvi_update_data_island_ptr(struct dvi_scanline_dma_list *dma_list, data_island_stream_t *stream) {
    for (int i = 0; i < N_TMDS_LANES; ++i) {
        dma_cb_t *cblist = dvi_lane_from_list(dma_list, i);
        uint32_t *src = stream->data[i];

        if (i == TMDS_SYNC_LANE) {
            cblist[1].read_addr = src;
        } else {
            cblist[2].read_addr = src;
        }
    }
}

void dvi_audio_sample_buffer_set(struct dvi_inst *inst, audio_sample_t *buffer, int size) {
    audio_ring_set(&inst->audio_ring, buffer, size);
}

// video_freq: video sampling frequency
// audio_freq: audio sampling frequency
// CTS: Cycle Time Stamp
// N: HDMI Constant
// 128 * audio_freq = video_freq * N / CTS
// e.g.: video_freq = 23495525, audio_freq = 44100 , CTS = 28000, N = 6727 
void dvi_set_audio_freq(struct dvi_inst *inst, int audio_freq, int cts, int n) {
    inst->audio_freq = audio_freq;
    set_audio_clock_regeneration(&inst->audio_clock_regeneration, cts, n);
    set_audio_info_frame(&inst->audio_info_frame, audio_freq);
    uint pixelClock =   dvi_timing_get_pixel_clock(inst->timing);
    uint nPixPerFrame = dvi_timing_get_pixels_per_frame(inst->timing);
    uint nPixPerLine =  dvi_timing_get_pixels_per_line(inst->timing);
    inst->samples_per_frame  = (uint64_t)(audio_freq) * nPixPerFrame / pixelClock;
    inst->samples_per_line16 = (uint64_t)(audio_freq) * nPixPerLine * 65536 / pixelClock;
    dvi_enable_data_island(inst);
}

void dvi_wait_for_valid_line(struct dvi_inst *inst) {
    uint32_t *tmdsbuf = NULL;
    queue_peek_blocking_u32(&inst->q_colour_valid, &tmdsbuf);
}

bool dvi_update_data_packet_(struct dvi_inst *inst, data_packet_t *packet) {
    if (inst->samples_per_frame == 0) {
        return false;
    }

    inst->audio_sample_pos += inst->samples_per_line16;
    if (inst->timing_state.v_state == DVI_STATE_FRONT_PORCH) {
        if (inst->timing_state.v_ctr == 0) {
            if (inst->dvi_frame_count & 1) {
                *packet = inst->avi_info_frame;
            } else {
                *packet = inst->audio_info_frame;
            }
            inst->left_audio_sample_count = inst->samples_per_frame;

            return true;
        } else if (inst->timing_state.v_ctr == 1) {
            *packet = inst->audio_clock_regeneration;

            return true;
        }
    }
    int sample_pos_16 = inst->audio_sample_pos >> 16;
    int read_size = get_read_size(&inst->audio_ring, false);
    int n = MAX(0, MIN(4, MIN(sample_pos_16, read_size)));
    inst->audio_sample_pos -= n << 16;
    if (n) {
        audio_sample_t *audio_sample_ptr = get_read_pointer(&inst->audio_ring);
        inst->audio_frame_count = set_audio_sample(packet, audio_sample_ptr, n, inst->audio_frame_count);
        increase_read_pointer(&inst->audio_ring, n);
        
        return true;
    }

    return false;
}

void dvi_update_data_packet(struct dvi_inst *inst) {
    data_packet_t packet;
    if (!dvi_update_data_packet_(inst, &packet)) {
        set_null(&packet);
    }
    bool vsync = inst->timing_state.v_state == DVI_STATE_SYNC;
    encode(&inst->next_data_stream, &packet, inst->timing->v_sync_polarity == vsync, inst->timing->h_sync_polarity);
}