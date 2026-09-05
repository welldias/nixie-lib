#ifndef NIXIE_SVGRASTER_SVG_IR_H
#define NIXIE_SVGRASTER_SVG_IR_H

#include "../color.h"
#include "../geometry.h"

/*
 * Flat shape-list IR produced by svg_parse.c and consumed by svg_to_png.c.
 * Every shape's coordinates are already in final absolute (SVG user-unit)
 * space -- any <g transform="..."> or per-element transform="..." has
 * already been baked in by the parser, EXCEPT text rotation (rotate() on a
 * <text> element), which the rasterizer must apply to the rendered glyph
 * bitmap rather than to the anchor point alone.
 */

typedef enum nixie_svg_shape_kind {
    NIXIE_SVG_SHAPE_RECT,
    NIXIE_SVG_SHAPE_CIRCLE,
    NIXIE_SVG_SHAPE_ELLIPSE,
    NIXIE_SVG_SHAPE_LINE,
    NIXIE_SVG_SHAPE_POLYLINE,
    NIXIE_SVG_SHAPE_POLYGON,
    NIXIE_SVG_SHAPE_PATH,
    NIXIE_SVG_SHAPE_TEXT
} nixie_svg_shape_kind_t;

typedef enum nixie_svg_text_anchor {
    NIXIE_SVG_ANCHOR_START = 0,
    NIXIE_SVG_ANCHOR_MIDDLE,
    NIXIE_SVG_ANCHOR_END
} nixie_svg_text_anchor_t;

typedef enum nixie_path_op_kind {
    NIXIE_PATHOP_MOVE,
    NIXIE_PATHOP_LINE,
    NIXIE_PATHOP_QUAD,
    NIXIE_PATHOP_CUBIC,
    NIXIE_PATHOP_CLOSE
} nixie_path_op_kind_t;

typedef struct nixie_path_op {
    nixie_path_op_kind_t kind;
    nixie_point_t p;      /* endpoint (MOVE/LINE/QUAD/CUBIC); ignored for CLOSE */
    nixie_point_t c1, c2; /* control points; c1 used by QUAD, c1+c2 by CUBIC */
} nixie_path_op_t;

typedef struct nixie_svg_paint {
    nixie_rgb_t fill;
    int has_fill;
    nixie_rgb_t stroke;
    int has_stroke;
    double stroke_width;
    double opacity;          /* 0..1, default 1 */
    double dash_on, dash_off; /* 0,0 == solid */
} nixie_svg_paint_t;

typedef struct nixie_svg_marker_ref {
    char id[64];
    int present;
} nixie_svg_marker_ref_t;

typedef struct nixie_svg_shape {
    nixie_svg_shape_kind_t kind;
    nixie_svg_paint_t paint;

    /* RECT */
    double x, y, w, h, rx, ry;
    /* CIRCLE (r) / ELLIPSE (rx, ry) share cx, cy */
    double cx, cy, r;
    /* LINE */
    double x1, y1, x2, y2;
    /* POLYLINE / POLYGON */
    nixie_point_t *points;
    int point_count;
    /* PATH */
    nixie_path_op_t *path_ops;
    int path_op_count;
    /* LINE / POLYLINE only */
    nixie_svg_marker_ref_t marker_start, marker_end;

    /* TEXT */
    char *text;
    double text_x, text_y;
    double font_size;
    int font_weight; /* numeric, e.g. 400/700 */
    int italic;
    int underline;
    nixie_svg_text_anchor_t text_anchor;
    /* rotate(deg,cx,cy) applied directly to this <text> element, if any --
     * deferred to raster time since it rotates rendered glyph pixels, not
     * just the anchor point. */
    int has_rotation;
    double rotate_deg;
    nixie_point_t rotate_center;
} nixie_svg_shape_t;

typedef struct nixie_svg_marker_def {
    char id[64];
    nixie_point_t *points;
    int point_count;
    double ref_x, ref_y;
    nixie_rgb_t fill;
    int has_fill;
} nixie_svg_marker_def_t;

typedef struct nixie_svg_document {
    double width, height;
    nixie_rgb_t background;
    int has_background;
    nixie_svg_shape_t *shapes;
    int shape_count;
    nixie_svg_marker_def_t *markers;
    int marker_count;
} nixie_svg_document_t;

#endif /* NIXIE_SVGRASTER_SVG_IR_H */
