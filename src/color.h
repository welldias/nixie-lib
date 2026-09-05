#ifndef NIXIE_COLOR_H
#define NIXIE_COLOR_H

typedef struct nixie_rgb {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} nixie_rgb_t;

/* Parses "#RGB" or "#RRGGBB" (leading '#' optional). Returns 0 on success. */
int nixie_parse_hex(const char *hex, nixie_rgb_t *out);

/* Writes "#RRGGBB\0" (8 bytes) into out. */
void nixie_format_hex(nixie_rgb_t c, char out[8]);

/*
 * Mixes fg into bg at pct percent (0-100), replicating the weighted RGB lerp
 * beautiful-mermaid's ASCII backend uses in place of CSS color-mix().
 */
nixie_rgb_t nixie_mix_rgb(nixie_rgb_t fg, nixie_rgb_t bg, int pct);

#endif /* NIXIE_COLOR_H */
