#include "text_metrics.h"

#include <string.h>

size_t nixie_utf8_decode(const char *s, uint32_t *cp_out) {
    unsigned char b0 = (unsigned char)s[0];

    if (b0 < 0x80) {
        *cp_out = b0;
        return 1;
    }

    int extra;
    uint32_t cp;
    if ((b0 & 0xE0) == 0xC0) {
        extra = 1;
        cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2;
        cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        extra = 3;
        cp = b0 & 0x07;
    } else {
        /* Invalid lead byte -- fall back to a single raw byte. */
        *cp_out = b0;
        return 1;
    }

    for (int i = 1; i <= extra; i++) {
        unsigned char b = (unsigned char)s[i];
        if (b == '\0' || (b & 0xC0) != 0x80) {
            *cp_out = b0;
            return 1;
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    *cp_out = cp;
    return (size_t)(extra + 1);
}

static int is_combining_mark(uint32_t c) {
    return (c >= 0x0300 && c <= 0x036f) ||
           (c >= 0x1ab0 && c <= 0x1aff) ||
           (c >= 0x1dc0 && c <= 0x1dff) ||
           (c >= 0x20d0 && c <= 0x20ff) ||
           (c >= 0xfe20 && c <= 0xfe2f);
}

/* CJK/Hangul + a coarse emoji-block approximation: beautiful-mermaid uses a
 * full \p{Emoji_Presentation}/\p{Extended_Pictographic} regex; we approximate
 * with the common emoji block ranges instead of porting a Unicode property
 * table into C. */
static int is_fullwidth(uint32_t c) {
    return (c >= 0x1100 && c <= 0x115f) ||
           (c >= 0x2e80 && c <= 0x2eff) ||
           (c >= 0x2f00 && c <= 0x2fdf) ||
           (c >= 0x3000 && c <= 0x303f) ||
           (c >= 0x3040 && c <= 0x309f) ||
           (c >= 0x30a0 && c <= 0x30ff) ||
           (c >= 0x3100 && c <= 0x312f) ||
           (c >= 0x3130 && c <= 0x318f) ||
           (c >= 0x3190 && c <= 0x31ff) ||
           (c >= 0x3200 && c <= 0x33ff) ||
           (c >= 0x3400 && c <= 0x4dbf) ||
           (c >= 0x4e00 && c <= 0x9fff) ||
           (c >= 0xac00 && c <= 0xd7af) ||
           (c >= 0xf900 && c <= 0xfaff) ||
           (c >= 0xff00 && c <= 0xff60) ||
           (c >= 0xffe0 && c <= 0xffe6) ||
           (c >= 0x1f300 && c <= 0x1faff) || /* emoji blocks (approximation) */
           (c >= 0x2600 && c <= 0x27bf) ||   /* misc symbols / dingbats */
           c >= 0x20000;
}

double nixie_char_width(uint32_t c) {
    if (is_combining_mark(c)) return 0.0;
    if (is_fullwidth(c)) return 2.0;
    if (c == ' ') return 0.3;

    if (c > 0x7f) {
        /* Non-ASCII, non-fullwidth (accented Latin, etc.): treat as average. */
        return 1.0;
    }

    char ch = (char)c;

    static const char *very_wide = "WM";
    static const char *wide = "WMwm@%";
    static const char *narrow = "iltfjI1!|.,:;'";
    static const char *semi_narrow_punct = "()[]{}/\\-\"`";

    if (strchr(very_wide, ch) != NULL) return 1.5;
    if (strchr(wide, ch) != NULL) return 1.2;
    if (strchr(narrow, ch) != NULL) return 0.4;
    if (strchr(semi_narrow_punct, ch) != NULL) return 0.5;
    if (ch == 'r') return 0.8;
    if (c >= 'A' && c <= 'Z') return 1.2;
    if (c >= '0' && c <= '9') return 1.0;

    return 1.0;
}

double nixie_measure_text_width(const char *text, double font_size, int font_weight) {
    if (text == NULL) {
        return 0.0;
    }

    double base_ratio = font_weight >= 600 ? 0.60 : font_weight >= 500 ? 0.57 : 0.54;

    double total = 0.0;
    const char *p = text;
    while (*p != '\0') {
        uint32_t cp;
        p += nixie_utf8_decode(p, &cp);
        total += nixie_char_width(cp);
    }

    double min_padding = font_size * 0.15;
    return total * font_size * base_ratio + min_padding;
}

nixie_multiline_metrics_t nixie_measure_multiline(
    nixie_arena_t *arena, const char *text, double font_size, int font_weight) {
    nixie_multiline_metrics_t m;
    m.width = 0.0;
    m.height = 0.0;
    m.line_height = font_size * NIXIE_LINE_HEIGHT_RATIO;
    m.lines = NULL;
    m.line_count = 0;

    if (text == NULL) {
        text = "";
    }

    /* Count lines first. */
    size_t count = 1;
    for (const char *p = text; *p != '\0'; p++) {
        if (*p == '\n') count++;
    }

    const char **lines = (const char **)nixie_arena_alloc(arena, count * sizeof(char *));

    size_t idx = 0;
    const char *line_start = text;
    for (const char *p = text;; p++) {
        if (*p == '\n' || *p == '\0') {
            size_t len = (size_t)(p - line_start);
            char *line = nixie_arena_strndup(arena, line_start, len);
            lines[idx++] = line;

            double w = nixie_measure_text_width(line, font_size, font_weight);
            if (w > m.width) m.width = w;

            if (*p == '\0') break;
            line_start = p + 1;
        }
    }

    m.lines = lines;
    m.line_count = count;
    m.height = (double)count * m.line_height;

    return m;
}
