#ifndef NIXIE_SVGRASTER_SVG_TO_PNG_H
#define NIXIE_SVGRASTER_SVG_TO_PNG_H

#include <nixie/nixie.h>

/*
 * Orchestrates svg_parse -> marker resolution -> shape drawing (document
 * order) -> PNG encoding. Both public entry points (nixie_render_png,
 * nixie_svg_to_png) funnel into this after nixie_render_png first calls
 * nixie_render_svg() to obtain svg_text.
 */
nixie_png_result_t nixie_svg_to_png_impl(const char *svg_text, const char *font_path, double scale);

#endif /* NIXIE_SVGRASTER_SVG_TO_PNG_H */
