// Fast scanout

// Every op starts with this:
//    uint16_t count;
//    uint16_t op;

// Where count is number of 2-pixel chunks
// op = 0: stop
// op = 1: solid color, next word is 12-bit rgb
// op = 2: solid gray, next word is gray (0..15)
// op = 3: 1-bit palette color, next word is palette

struct scanlist {
    uint16_t count;
    uint16_t op;
};

void fast_scanout(void *scanlist, uint32_t *inbuf, uint32_t *outbuf, uint32_t stride);

void tmds_scan_stop();

void tmds_scan_solid();
void tmds_scan_solid_gray();
void tmds_scan_solid_tmds();

void tmds_scan_1bpp_pal();

void tmds_scan(uint32_t *scanlist, uint32_t *inbuf, uint32_t *outbuf, uint32_t stride);
