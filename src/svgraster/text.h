#ifndef NIXIE_SVGRASTER_TEXT_H
#define NIXIE_SVGRASTER_TEXT_H

#include "canvas.h"
#include "svg_ir.h"

/*
 * A loaded caller-supplied font. `stbtt_info` is an opaque heap-allocated
 * stbtt_fontinfo* -- only text.c needs to know about stb_truetype.h.
 */
typedef struct nixie_font {
    unsigned char *file_data;
    void *stbtt_info;
    int ok;
} nixie_font_t;

/* Reads font_path fully into memory and initializes it via stb_truetype.
 * On failure font.ok == 0 and nothing is leaked. */
nixie_font_t nixie_font_load(const char *font_path);
void nixie_font_free(nixie_font_t *font);

/*
 * Draws one TEXT shape onto canvas using `font` (must be loaded and ok).
 * shape->text_x/text_y/font_size/rotate_center are assumed to already be in
 * final device-pixel space (the parser bakes the render scale in once, up
 * front).
 */
void nixie_draw_text(nixie_canvas_t *canvas, const nixie_font_t *font, const nixie_svg_shape_t *shape);

#endif /* NIXIE_SVGRASTER_TEXT_H */
