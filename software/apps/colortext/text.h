struct text_line {
    Run *runs;
    uint width;
    uint32_t palette[16];
};

void setup_text_line(struct text_line *line, const char *text, uint max_w);
void render_text_scanline(uint32_t *outp, Run *runs, uint y);
