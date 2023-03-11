#include "audio_ring.h"
#include <hardware/sync.h>

void audio_ring_set(audio_ring_t *audio_ring, audio_sample_t *buffer, uint32_t size) {
    assert(size > 1);
    audio_ring->buffer = buffer;
    audio_ring->size   = size;
    audio_ring->read   = 0;
    audio_ring->write  = 0;
}

uint32_t get_write_size(audio_ring_t *audio_ring, bool full) {
    __mem_fence_acquire();
    uint32_t rp = audio_ring->read;
    uint32_t wp = audio_ring->write;
    if (wp < rp) {
        return rp - wp - 1;
    } else {
        uint32_t size = audio_ring->size - wp;
        return full ? ( size - wp + rp - 1) : (size - wp - (rp == 0 ? 1 : 0));
    }   
}

uint32_t get_read_size(audio_ring_t *audio_ring, bool full) {
    __mem_fence_acquire();
    uint32_t rp = audio_ring->read;
    uint32_t wp = audio_ring->write;
    
    if (wp < rp) {
        return audio_ring->size - rp + (full ? wp : 0);
    } else {
        return wp - rp;
    }    
}

void increase_write_pointer(audio_ring_t *audio_ring, uint32_t size) {
    audio_ring->write = (audio_ring->write + size) & (audio_ring->size - 1);
    __mem_fence_release();
}

void increase_read_pointer(audio_ring_t *audio_ring, uint32_t size) {
    audio_ring->read = (audio_ring->read + size) & (audio_ring->size - 1);
    __mem_fence_release();
}

void set_write_offset(audio_ring_t *audio_ring, uint32_t v) {
    audio_ring->write = v;
    __mem_fence_release();
}

void set_read_offset(audio_ring_t *audio_ring, uint32_t v) {
    audio_ring->read = v;
    __mem_fence_release();
}