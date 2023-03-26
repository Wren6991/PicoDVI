#ifndef _DVI_H
#define _DVI_H

#include "pico/util/queue.h"
#include "dvi_config_defs.h"
#include "dvi_timing.h"
#include "dvi_serialiser.h"
#include "util_queue_u32_inline.h"
#include "data_packet.h"

#define TMDS_SYNC_LANE  0 // blue!
#ifndef TMDS_CHANNELS
    #define TMDS_CHANNELS   3
#endif

typedef void (*dvi_callback_t)(uint);

struct dvi_inst {
	// Config ---
	const struct dvi_timing *timing;
	struct dvi_lane_dma_cfg dma_cfg[N_TMDS_LANES];
	struct dvi_timing_state timing_state;
	struct dvi_serialiser_cfg ser_cfg;
    dvi_blank_t blank_settings;
	// Called in the DMA IRQ once per scanline -- careful with the run time!
	dvi_callback_t scanline_callback;

	// State ---
	struct dvi_scanline_dma_list dma_list_vblank_sync;
	struct dvi_scanline_dma_list dma_list_vblank_nosync;
	struct dvi_scanline_dma_list dma_list_active;
	struct dvi_scanline_dma_list dma_list_error;
    struct dvi_scanline_dma_list dma_list_active_blank;

	// After a TMDS buffer has been enqueue via a control block for the last
	// time, two IRQs must go by before freeing. The first indicates the control
	// block for this buf has been loaded, and the second occurs some time after
	// the actual data DMA transfer has completed.
	uint32_t *tmds_buf_release[2];

	// Remember how far behind the source is on TMDS scanlines, so we can output
	// solid colour until they catch up (rather than dying spectacularly)
	uint late_scanline_ctr;

	// Encoded scanlines:
	queue_t q_tmds_valid;
	queue_t q_tmds_free;

	// Either scanline buffers or frame buffers:
	queue_t q_colour_valid;
	queue_t q_colour_free;
    bool    dvi_started;
    uint    dvi_frame_count;

    //Data Packet related
    data_packet_t avi_info_frame;
    data_packet_t audio_clock_regeneration;
    data_packet_t audio_info_frame;
    int audio_freq;
    int samples_per_frame;
    int samples_per_line16;
    
    bool data_island_is_enabled;
    bool scanline_is_enabled;
    data_island_stream_t next_data_stream;
    audio_ring_t  audio_ring;

    int left_audio_sample_count;
    int audio_sample_pos;
    int audio_frame_count;
};

// Reports DVI status 1: active 0: inactive
inline bool dvi_is_started(struct dvi_inst *inst) {
    return inst->dvi_started;
}

// Set up data structures and hardware for DVI.
void dvi_init(struct dvi_inst *inst, uint spinlock_tmds_queue, uint spinlock_colour_queue);

// Call this after calling dvi_init(). DVI DMA interrupts will be routed to
// whichever core called this function. Registers an exclusive IRQ handler.
void dvi_register_irqs_this_core(struct dvi_inst *inst, uint irq_num);

// Unregisters DVI irq callbacks for this core
void dvi_unregister_irqs_this_core(struct dvi_inst *inst, uint irq_num);

// Start actually wiggling TMDS pairs. Call this once you have initialised the
// DVI, have registered the IRQs, and are producing rendered scanlines.
void dvi_start(struct dvi_inst *inst);

//Stops DVI pairs generations
void dvi_stop(struct dvi_inst *inst);

//Waits for a valid line
void dvi_wait_for_valid_line(struct dvi_inst *inst);

// TMDS encode worker function: core enters and doesn't leave, but still
// responds to IRQs. Repeatedly pop a scanline buffer from q_colour_valid,
// TMDS encode it, and pass it to the tmds valid queue.
void dvi_scanbuf_main_8bpp(struct dvi_inst *inst);
void dvi_scanbuf_main_16bpp(struct dvi_inst *inst);

// Same as above, but each q_colour_valid entry is a framebuffer
void dvi_framebuf_main_8bpp(struct dvi_inst *inst);
void dvi_framebuf_main_16bpp(struct dvi_inst *inst);

// Data island (and audio) related api
void dvi_audio_init(struct dvi_inst *inst);
void dvi_enable_data_island(struct dvi_inst *inst);
void dvi_update_data_island_ptr(struct dvi_scanline_dma_list *dma_list, data_island_stream_t *stream);
void dvi_audio_sample_buffer_set(struct dvi_inst *inst, audio_sample_t *buffer, int size);
void dvi_set_audio_freq(struct dvi_inst *inst, int audio_freq, int cts, int n);
void dvi_update_data_packet(struct dvi_inst *inst);
inline void dvi_set_scanline(struct dvi_inst *inst, bool value) {
    inst->scanline_is_enabled = value;
}
inline dvi_blank_t *dvi_get_blank_settings(struct dvi_inst *inst) {
    return &inst->blank_settings;
}
#endif
