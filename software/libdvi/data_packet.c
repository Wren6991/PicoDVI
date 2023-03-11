#include "data_packet.h"
#include <string.h>

// Compute 8 Parity Start
// Parity table is build statically with the following code
// for (int i = 0; i < 256; ++i){v_[i] = (i ^ (i >> 1) ^ (i >> 2) ^ (i >> 3) ^ (i >> 4) ^ (i >> 5) ^ (i >> 6) ^ (i >> 7)) & 1;}
const uint8_t parityTable[32] = { 0x96, 0x69, 0x69, 0x96, 0x69, 0x96, 0x96, 0x69, 0x69, 0x96, 0x96, 0x69, 0x96, 0x69, 0x69, 0x96, 0x69, 0x96, 0x96, 0x69, 0x96, 0x69, 0x69, 0x96, 0x96, 0x69, 0x69, 0x96, 0x69, 0x96, 0x96, 0x69 };
bool __not_in_flash_func(compute8)(uint8_t index) { 
    return (parityTable[index / 8] >> (index % 8)) & 0x01; 
}
bool __not_in_flash_func(compute8_2)(uint8_t index1, uint8_t index2) {
    return compute8(index1) ^ compute8(index2);
}
bool __not_in_flash_func(compute8_3)(uint8_t index1, uint8_t index2, uint8_t index3) {
     return compute8(index1) ^ compute8(index2) ^ compute8(index3);
}
// Compute 8 Parity End

// BCH Encoding Start
const uint8_t __not_in_flash_func(bchTable_)[256] = {
    0x00, 0xd9, 0xb5, 0x6c, 0x6d, 0xb4, 0xd8, 0x01, 
    0xda, 0x03, 0x6f, 0xb6, 0xb7, 0x6e, 0x02, 0xdb, 
    0xb3, 0x6a, 0x06, 0xdf, 0xde, 0x07, 0x6b, 0xb2, 
    0x69, 0xb0, 0xdc, 0x05, 0x04, 0xdd, 0xb1, 0x68, 
    0x61, 0xb8, 0xd4, 0x0d, 0x0c, 0xd5, 0xb9, 0x60, 
    0xbb, 0x62, 0x0e, 0xd7, 0xd6, 0x0f, 0x63, 0xba, 
    0xd2, 0x0b, 0x67, 0xbe, 0xbf, 0x66, 0x0a, 0xd3, 
    0x08, 0xd1, 0xbd, 0x64, 0x65, 0xbc, 0xd0, 0x09, 
    0xc2, 0x1b, 0x77, 0xae, 0xaf, 0x76, 0x1a, 0xc3, 
    0x18, 0xc1, 0xad, 0x74, 0x75, 0xac, 0xc0, 0x19, 
    0x71, 0xa8, 0xc4, 0x1d, 0x1c, 0xc5, 0xa9, 0x70, 
    0xab, 0x72, 0x1e, 0xc7, 0xc6, 0x1f, 0x73, 0xaa, 
    0xa3, 0x7a, 0x16, 0xcf, 0xce, 0x17, 0x7b, 0xa2, 
    0x79, 0xa0, 0xcc, 0x15, 0x14, 0xcd, 0xa1, 0x78, 
    0x10, 0xc9, 0xa5, 0x7c, 0x7d, 0xa4, 0xc8, 0x11, 
    0xca, 0x13, 0x7f, 0xa6, 0xa7, 0x7e, 0x12, 0xcb, 
    0x83, 0x5a, 0x36, 0xef, 0xee, 0x37, 0x5b, 0x82, 
    0x59, 0x80, 0xec, 0x35, 0x34, 0xed, 0x81, 0x58, 
    0x30, 0xe9, 0x85, 0x5c, 0x5d, 0x84, 0xe8, 0x31, 
    0xea, 0x33, 0x5f, 0x86, 0x87, 0x5e, 0x32, 0xeb, 
    0xe2, 0x3b, 0x57, 0x8e, 0x8f, 0x56, 0x3a, 0xe3, 
    0x38, 0xe1, 0x8d, 0x54, 0x55, 0x8c, 0xe0, 0x39, 
    0x51, 0x88, 0xe4, 0x3d, 0x3c, 0xe5, 0x89, 0x50, 
    0x8b, 0x52, 0x3e, 0xe7, 0xe6, 0x3f, 0x53, 0x8a, 
    0x41, 0x98, 0xf4, 0x2d, 0x2c, 0xf5, 0x99, 0x40, 
    0x9b, 0x42, 0x2e, 0xf7, 0xf6, 0x2f, 0x43, 0x9a, 
    0xf2, 0x2b, 0x47, 0x9e, 0x9f, 0x46, 0x2a, 0xf3, 
    0x28, 0xf1, 0x9d, 0x44, 0x45, 0x9c, 0xf0, 0x29, 
    0x20, 0xf9, 0x95, 0x4c, 0x4d, 0x94, 0xf8, 0x21, 
    0xfa, 0x23, 0x4f, 0x96, 0x97, 0x4e, 0x22, 0xfb, 
    0x93, 0x4a, 0x26, 0xff, 0xfe, 0x27, 0x4b, 0x92, 
    0x49, 0x90, 0xfc, 0x25, 0x24, 0xfd, 0x91, 0x48, 
};

int __not_in_flash_func(encode_BCH_3)(const uint8_t *p) {
    uint8_t v = bchTable_[p[0]];
    v = bchTable_[p[1] ^ v];
    v = bchTable_[p[2] ^ v];
    return v;
}

int __not_in_flash_func(encode_BCH_7)(const uint8_t *p) {
    uint8_t v = bchTable_[p[0]];
    v = bchTable_[p[1] ^ v];
    v = bchTable_[p[2] ^ v];
    v = bchTable_[p[3] ^ v];
    v = bchTable_[p[4] ^ v];
    v = bchTable_[p[5] ^ v];
    v = bchTable_[p[6] ^ v];
    return v;
}
// BCH Encoding End

// TERC4 Start
uint16_t __not_in_flash_func(TERC4Syms_)[16] = {
    0b1010011100,
    0b1001100011,
    0b1011100100,
    0b1011100010,
    0b0101110001,
    0b0100011110,
    0b0110001110,
    0b0100111100,
    0b1011001100,
    0b0100111001,
    0b0110011100,
    0b1011000110,
    0b1010001110,
    0b1001110001,
    0b0101100011,
    0b1011000011,
};

uint32_t __not_in_flash_func(makeTERC4x2Char)(int i) { return TERC4Syms_[i] | (TERC4Syms_[i] << 10); }
uint32_t __not_in_flash_func(makeTERC4x2Char_2)(int i0, int i1) { return TERC4Syms_[i0] | (TERC4Syms_[i1] << 10); }
#define TERC4_0x2CharSym_ 0x000A729C // Build time generated -> makeTERC4x2Char(0);
#define dataGaurdbandSym_ 0x0004CD33 // Build time generated -> 0b0100110011'0100110011;
uint32_t __not_in_flash_func(defaultDataPacket12_)[N_DATA_ISLAND_WORDS] = {
    dataGaurdbandSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    TERC4_0x2CharSym_,
    dataGaurdbandSym_,
};

// This table is built in compilation time from a function that uses makeTERC4x2Char 
uint32_t __not_in_flash_func(defaultDataPackets0_)[4][N_DATA_ISLAND_WORDS] = {
    { 0xa3a8e, 0xa729c, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xb32cc, 0xa3a8e},
    { 0x9c671, 0x98e63, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x4e539, 0x9c671}, 
    { 0x58d63, 0xb92e4, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x6719c, 0x58d63}, 
    { 0xb0ec3, 0xb8ae2, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb1ac6, 0xb0ec3}
};

uint32_t *__not_in_flash_func(getDefaultDataPacket0)(bool vsync, bool hsync) {
    return defaultDataPackets0_[(vsync << 1) | hsync];
}

// TERC4 End

void __not_in_flash_func(compute_header_parity)(data_packet_t *data_packet) {
    data_packet->header[3] = encode_BCH_3(data_packet->header);
}

void __not_in_flash_func(compute_subpacket_parity)(data_packet_t *data_packet, int i) {
    data_packet->subpacket[i][7] = encode_BCH_7(data_packet->subpacket[i]);
}

void __not_in_flash_func(compute_parity)(data_packet_t *data_packet) {
    compute_header_parity(data_packet);
    compute_subpacket_parity(data_packet, 0);
    compute_subpacket_parity(data_packet, 1);
    compute_subpacket_parity(data_packet, 2);
    compute_subpacket_parity(data_packet, 3);
}

void __not_in_flash_func(compute_info_frame_checkSum)(data_packet_t *data_packet) {
    int s = 0;
    for (int i = 0; i < 3; ++i)
    {
        s += data_packet->header[i];
    }
    int n = data_packet->header[2] + 1;
    for (int j = 0; j < 4; ++j)
    {
        for (int i = 0; i < 7 && n; ++i, --n)
        {
            s += data_packet->subpacket[j][i];
        }
    }
    data_packet->subpacket[0][0] = -s;
}

void __not_in_flash_func(encode_header)(const data_packet_t *data_packet, uint32_t *dst, int hv, bool firstPacket) {
    int hv1 = hv | 8;
    if (!firstPacket) { 
        hv = hv1;
    }
    for (int i = 0; i < 4; ++i) {
        uint8_t h = data_packet->header[i];
        dst[0] = makeTERC4x2Char_2(((h << 2) & 4) | hv,  ((h << 1) & 4) | hv1);
        dst[1] = makeTERC4x2Char_2((h & 4) | hv1,        ((h >> 1) & 4) | hv1);
        dst[2] = makeTERC4x2Char_2(((h >> 2) & 4) | hv1, ((h >> 3) & 4) | hv1);
        dst[3] = makeTERC4x2Char_2(((h >> 4) & 4) | hv1, ((h >> 5) & 4) | hv1);
        dst += 4;
        hv = hv1;
    }
}

void __not_in_flash_func(encode_subpacket)(const data_packet_t *data_packet, uint32_t *dst1, uint32_t *dst2) {
    for (int i = 0; i < 8; ++i) {
        uint32_t v = (data_packet->subpacket[0][i] << 0)  | (data_packet->subpacket[1][i] << 8) |
                     (data_packet->subpacket[2][i] << 16) | (data_packet->subpacket[3][i] << 24);
        uint32_t t = (v ^ (v >> 7)) & 0x00aa00aa;
        v = v ^ t ^ (t << 7);
        t = (v ^ (v >> 14)) & 0x0000cccc;
        v = v ^ t ^ (t << 14);
        // 01234567 89abcdef ghijklmn opqrstuv
        // 08go4cks 19hp5dlt 2aiq6emu 3bjr7fnv
        dst1[0] = makeTERC4x2Char_2((v >> 0) & 15,  (v >> 16) & 15);
        dst1[1] = makeTERC4x2Char_2((v >> 4) & 15,  (v >> 20) & 15);
        dst2[0] = makeTERC4x2Char_2((v >> 8) & 15,  (v >> 24) & 15);
        dst2[1] = makeTERC4x2Char_2((v >> 12) & 15, (v >> 28) & 15);
        dst1 += 2;
        dst2 += 2;
    }
}

void __not_in_flash_func(set_null)(data_packet_t *data_packet) {
    memset(data_packet, 0, sizeof(data_packet_t));
}

int  __not_in_flash_func(set_audio_sample)(data_packet_t *data_packet, const audio_sample_t *p, int n, int frameCt) {
    const int layout = 0;
    const int samplePresent = (1 << n) - 1;
    const int B = frameCt < 4 ? 1 << frameCt : 0;
    data_packet->header[0] = 2;
    data_packet->header[1] = (layout << 4) | samplePresent;
    data_packet->header[2] = B << 4;
    compute_header_parity(data_packet);

    for (int i = 0; i < n; ++i)
    {
        const int16_t l = (*p).channels[0];
        const int16_t r = (*p).channels[1];
        const uint8_t vuc = 1; // valid
        uint8_t *d = data_packet->subpacket[i];
        d[0] = 0;
        d[1] = l;
        d[2] = l >> 8;
        d[3] = 0;
        d[4] = r;
        d[5] = r >> 8;

        bool pl = compute8_3(d[1], d[2], vuc);
        bool pr = compute8_3(d[4], d[5], vuc);
        d[6] = (vuc << 0) | (pl << 3) | (vuc << 4) | (pr << 7);
        compute_subpacket_parity(data_packet, i);
        ++p;
        // channel status (is it relevant?)
    }
    memset(data_packet->subpacket[n], 0, sizeof(data_packet->subpacket[0]) * (4 - n));
    // dump();

    frameCt -= n;
    if (frameCt < 0) {
        frameCt += 192;
    }
    return frameCt;
}

void set_audio_clock_regeneration(data_packet_t *data_packet, int cts, int n) {
    data_packet->header[0] = 1;
    data_packet->header[1] = 0;
    data_packet->header[2] = 0;
    compute_header_parity(data_packet);

    data_packet->subpacket[0][0] = 0;
    data_packet->subpacket[0][1] = cts >> 16;
    data_packet->subpacket[0][2] = cts >> 8;
    data_packet->subpacket[0][3] = cts;
    data_packet->subpacket[0][4] = n >> 16;
    data_packet->subpacket[0][5] = n >> 8;
    data_packet->subpacket[0][6] = n;
    compute_subpacket_parity(data_packet, 0);

    memcpy(data_packet->subpacket[1], data_packet->subpacket[0], sizeof(data_packet->subpacket[0]));
    memcpy(data_packet->subpacket[2], data_packet->subpacket[0], sizeof(data_packet->subpacket[0]));
    memcpy(data_packet->subpacket[3], data_packet->subpacket[0], sizeof(data_packet->subpacket[0]));
}

void set_audio_info_frame(data_packet_t *data_packet, int freq) {
    set_null(data_packet);
    data_packet->header[0] = 0x84;
    data_packet->header[1] = 1;  // version
    data_packet->header[2] = 10; // len

    const int cc = 1; // 2ch
    const int ct = 1; // IEC 60958 PCM
    const int ss = 1; // 16bit
    const int sf = freq == 48000 ? 3 : (freq == 44100 ? 2 : 0);
    const int ca = 0;  // FR, FL
    const int lsv = 0; // 0db
    const int dm_inh = 0;
    data_packet->subpacket[0][1] = cc | (ct << 4);
    data_packet->subpacket[0][2] = ss | (sf << 2);
    data_packet->subpacket[0][4] = ca;
    data_packet->subpacket[0][5] = (lsv << 3) | (dm_inh << 7);

    compute_info_frame_checkSum(data_packet);
    compute_parity(data_packet);
}

void set_AVI_info_frame(data_packet_t *data_packet, scan_info s, pixel_format y, colorimetry c, picture_aspect_ratio m,
    active_format_aspect_ratio r, RGB_quantization_range q, video_code vic) {
    set_null(data_packet);
    data_packet->header[0] = 0x82;
    data_packet->header[1] = 2;  // version
    data_packet->header[2] = 13; // len

        int sc = 0;
        // int sc = 3; // scaled hv

    data_packet->subpacket[0][1] = (int)(s) | (r == ACTIVE_FORMAT_ASPECT_RATIO_NO_DATA ? 0 : 16) | ((int)(y) << 5);
    data_packet->subpacket[0][2] = (int)(r) | ((int)(m) << 4) | ((int)(c) << 6);
    data_packet->subpacket[0][3] = sc | ((int)(q) << 2);
    data_packet->subpacket[0][4] = (int)(vic);

    compute_info_frame_checkSum(data_packet);
    compute_parity(data_packet);
}

void encode(data_island_stream_t *dst, const data_packet_t *packet, bool vsync, bool hsync) {
    int hv = (vsync ? 2 : 0) | (hsync ? 1 : 0);
    dst->data[0][0] = makeTERC4x2Char(0b1100 | hv);
    dst->data[1][0] = dataGaurdbandSym_;
    dst->data[2][0] = dataGaurdbandSym_;

    encode_header(packet, &dst->data[0][1], hv, true);
    encode_subpacket(packet, &dst->data[1][1], &dst->data[2][1]);

    dst->data[0][N_DATA_ISLAND_WORDS - 1] = makeTERC4x2Char(0b1100 | hv);
    dst->data[1][N_DATA_ISLAND_WORDS - 1] = dataGaurdbandSym_;
    dst->data[2][N_DATA_ISLAND_WORDS - 1] = dataGaurdbandSym_;
}