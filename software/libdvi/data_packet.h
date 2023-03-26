#ifndef DATA_PACKET_H
#define DATA_PACKET_H
#include "pico.h"
#include "audio_ring.h"

#define TMDS_CHANNELS        3
#define N_LINE_PER_DATA      2
#define W_GUARDBAND          2
#define W_PREAMBLE           8
#define W_DATA_PACKET        32

#ifndef DVI_SYMBOLS_PER_WORD
#define DVI_SYMBOLS_PER_WORD 2
#endif

#if DVI_SYMBOLS_PER_WORD != 1 && DVI_SYMBOLS_PER_WORD !=2
#error "Unsupported value for DVI_SYMBOLS_PER_WORD"
#endif


#define W_DATA_ISLAND        (W_GUARDBAND * 2 + W_DATA_PACKET)
#define N_DATA_ISLAND_WORDS  (W_DATA_ISLAND / DVI_SYMBOLS_PER_WORD)

typedef enum {
    SCAN_INFO_NO_DATA,
    OVERSCAN,
    UNDERSCAN
} scan_info;

typedef enum {
    RGB,
    YCBCR422,
    YCBCR444
} pixel_format;

typedef enum {
    COLORIMETRY_NO_DATA,
    ITU601,
    ITU709,
    EXTENDED
} colorimetry;

typedef enum {
    PIC_ASPECT_RATIO_NO_DATA,
    PIC_ASPECT_RATIO_4_3,
    PIC_ASPECT_RATIO_16_9
} picture_aspect_ratio;

typedef enum {
    ACTIVE_FORMAT_ASPECT_RATIO_NO_DATA = -1,
    SAME_AS_PAR = 8,
    ACTIVE_FORMAT_ASPECT_RATIO_4_3,
    ACTIVE_FORMAT_ASPECT_RATIO_16_9,
    ACTIVE_FORMAT_ASPECT_RATIO_14_9
} active_format_aspect_ratio;

typedef enum {
    DEFAULT,
    LIMITED,
    FULL
} RGB_quantization_range;

typedef enum {
    _640x480P60 = 1,
    _720x480P60 = 2,
    _1280x720P60 = 4,
    _1920x1080I60 = 5,
} video_code;

typedef struct data_packet {
    uint8_t header[4];
    uint8_t subpacket[4][8];
} data_packet_t;

typedef struct data_island_stream {
    uint32_t data[TMDS_CHANNELS][N_DATA_ISLAND_WORDS];
} data_island_stream_t;

// Functions related to the data_packet (requires a data_packet instance)
void compute_header_parity(data_packet_t *data_packet);
void compute_subpacket_parity(data_packet_t *data_packet, int i);
void compute_parity(data_packet_t *data_packet);
void compute_info_frame_checkSum(data_packet_t *data_packet);
void encode_header(const data_packet_t *data_packet, uint32_t *dst, int hv, bool firstPacket);
void encode_subpacket(const data_packet_t *data_packet, uint32_t *dst1, uint32_t *dst2);
void set_null(data_packet_t *data_packet);
int  set_audio_sample(data_packet_t *data_packet, const audio_sample_t *p, int n, int frameCt);
void set_audio_clock_regeneration(data_packet_t *data_packet, int CTS, int N);
void set_audio_info_frame(data_packet_t *data_packet, int freq);
void set_AVI_info_frame(data_packet_t *data_packet, scan_info s, pixel_format y, colorimetry c, picture_aspect_ratio m,
    active_format_aspect_ratio r, RGB_quantization_range q, video_code vic);

// Public Functions
extern uint32_t defaultDataPacket12_[N_DATA_ISLAND_WORDS];
inline uint32_t *getDefaultDataPacket12() {
    return defaultDataPacket12_;
}
uint32_t *getDefaultDataPacket0(bool vsync, bool hsync);
void encode(data_island_stream_t *dst, const data_packet_t *packet, bool vsync, bool hsync);
#endif
