#include "color.h"

#include <stdio.h>
#include <string.h>

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_byte(const char *s) {
    int hi = hex_nibble(s[0]);
    int lo = hex_nibble(s[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

int nixie_parse_hex(const char *hex, nixie_rgb_t *out) {
    if (hex == NULL || out == NULL) {
        return -1;
    }

    if (hex[0] == '#') {
        hex++;
    }

    size_t len = strlen(hex);

    if (len == 6) {
        int r = hex_byte(hex);
        int g = hex_byte(hex + 2);
        int b = hex_byte(hex + 4);
        if (r < 0 || g < 0 || b < 0) return -1;
        out->r = (unsigned char)r;
        out->g = (unsigned char)g;
        out->b = (unsigned char)b;
        return 0;
    }

    if (len == 3) {
        int r = hex_nibble(hex[0]);
        int g = hex_nibble(hex[1]);
        int b = hex_nibble(hex[2]);
        if (r < 0 || g < 0 || b < 0) return -1;
        out->r = (unsigned char)(r * 17);
        out->g = (unsigned char)(g * 17);
        out->b = (unsigned char)(b * 17);
        return 0;
    }

    return -1;
}

void nixie_format_hex(nixie_rgb_t c, char out[8]) {
    snprintf(out, 8, "#%02X%02X%02X", c.r, c.g, c.b);
}

static unsigned char mix_channel(unsigned char fg, unsigned char bg, int pct) {
    int value = (fg * pct + bg * (100 - pct) + 50) / 100;
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    return (unsigned char)value;
}

nixie_rgb_t nixie_mix_rgb(nixie_rgb_t fg, nixie_rgb_t bg, int pct) {
    nixie_rgb_t out;
    out.r = mix_channel(fg.r, bg.r, pct);
    out.g = mix_channel(fg.g, bg.g, pct);
    out.b = mix_channel(fg.b, bg.b, pct);
    return out;
}
