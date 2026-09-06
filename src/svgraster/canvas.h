#ifndef NIXIE_SVGRASTER_CANVAS_H
#define NIXIE_SVGRASTER_CANVAS_H

#include "../arena.h"
#include "../color.h"
#include "svg_path.h"

/* RGBA8 raster buffer, non-premultiplied, row-major top-to-bottom. Knows
 * nothing about SVG syntax -- consumes already-flattened polygons. */
typedef struct nixie_canvas {
    unsigned char *pixels; /* w*h*4 bytes */
    int w, h;
} nixie_canvas_t;

nixie_canvas_t *nixie_canvas_create(nixie_arena_t *arena, int w, int h, nixie_rgb_t background, int has_background);

/* Alpha-composites a single pixel (standard non-premultiplied "over"
 * blend). Shared by the fill/stroke rasterizer and the text blitter. */
void nixie_canvas_blend_pixel(nixie_canvas_t *canvas, int x, int y, nixie_rgb_t color, double alpha);

/*
 * Fills `polys` (one or more closed subpaths, nonzero winding rule) via
 * bounded per-shape 4x4 supersampling scoped to the shapes' own bbox, then
 * alpha-composites the result onto `canvas`.
 */
void nixie_canvas_fill_polygons(nixie_canvas_t *canvas, const nixie_polyline_list_t *polys, nixie_rgb_t color, double opacity);

/*
 * Expands `points` (an open or closed polyline) into a stroke outline
 * (segment quads + round join/cap discs at every vertex, dash-split first
 * when dash_on/dash_off > 0) and fills each piece via
 * nixie_canvas_fill_polygons. `arena` backs the temporary quad/disc point
 * lists.
 */
void nixie_canvas_stroke_polyline(nixie_canvas_t *canvas, nixie_arena_t *arena, const nixie_point_t *points, int count, int closed, double stroke_width, nixie_rgb_t color, double opacity, double dash_on, double dash_off);

#endif /* NIXIE_SVGRASTER_CANVAS_H */
