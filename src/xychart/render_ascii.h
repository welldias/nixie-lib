#ifndef NIXIE_XYCHART_RENDER_ASCII_H
#define NIXIE_XYCHART_RENDER_ASCII_H

#include "../arena.h"
#include "model.h"

typedef struct nixie_xy_ascii_options {
    int use_unicode; /* 1 = box-drawing glyphs, 0 = plain ASCII */
} nixie_xy_ascii_options_t;

/*
 * Renders an XY chart as ASCII/Unicode text art. Unlike the SVG path, this
 * operates directly on the parsed nixie_xy_chart_t (not the pixel-space
 * positioned model) and computes its own character-grid layout from
 * scratch -- pixel coordinates from nixie_xy_layout() don't translate to a
 * character grid, and beautiful-mermaid's own ASCII xychart backend makes
 * the same choice. Series are distinguished by position/legend text only
 * (no per-series color), consistent with nixie's other ASCII renderers.
 *
 * Returns a malloc'd string; free with nixie_free() (== free()).
 */
char *nixie_xy_render_ascii(nixie_arena_t *arena, const nixie_xy_chart_t *chart, const nixie_xy_ascii_options_t *opts);

#endif /* NIXIE_XYCHART_RENDER_ASCII_H */
