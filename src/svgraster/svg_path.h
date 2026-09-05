#ifndef NIXIE_SVGRASTER_SVG_PATH_H
#define NIXIE_SVGRASTER_SVG_PATH_H

#include "../arena.h"
#include "../geometry.h"
#include "svg_ir.h"

/* Arena-backed growable point array (doubling growth; old backing storage
 * is simply abandoned in the arena, which is fine for a short-lived
 * per-render pipeline). */
typedef struct nixie_point_list {
    nixie_point_t *points;
    int count;
    int cap;
} nixie_point_list_t;

void nixie_point_list_push(nixie_arena_t *arena, nixie_point_list_t *list, nixie_point_t p);

/* One flattened subpath: a polyline in final absolute coordinates, plus
 * whether the source path segment ended in a Z (closed) command. */
typedef struct nixie_polyline {
    nixie_point_list_t points;
    int closed;
} nixie_polyline_t;

typedef struct nixie_polyline_list {
    nixie_polyline_t *lines;
    int count;
    int cap;
} nixie_polyline_list_t;

void nixie_polyline_list_push(nixie_arena_t *arena, nixie_polyline_list_t *list, nixie_polyline_t line);

/*
 * Recursively subdivides cubic Bezier (p0,c1,c2,p3) via de Casteljau
 * splitting until the inner control points deviate from the p0-p3 chord by
 * less than `tolerance`, then appends the flattened points (excluding p0,
 * which the caller is assumed to have already appended) to `out`.
 */
void nixie_flatten_cubic(nixie_arena_t *arena, nixie_point_t p0, nixie_point_t c1, nixie_point_t c2, nixie_point_t p3,
                          double tolerance, int depth, nixie_point_list_t *out);

/*
 * Converts one shape's path_ops (possibly containing multiple M...Z
 * subpaths) into flattened polylines. Quadratics are promoted to cubics
 * internally (standard 2/3-rule) before flattening.
 */
nixie_polyline_list_t nixie_flatten_path(nixie_arena_t *arena, const nixie_path_op_t *ops, int op_count, double tolerance);

/*
 * Tessellates a rect (rx==ry==0 => plain rect; rx>0/ry>0 => rounded corners,
 * approximated as an ellipse arc per corner; rx>=w/2 or ry>=h/2 => stadium)
 * into a closed polygon. Segment count per quarter-circle scales with the
 * corner radius (see nixie_arc_segments_for_radius). All coordinates are
 * already in final device-pixel space -- the parser bakes the render scale
 * into every shape's geometry once, up front, so nothing downstream needs
 * a separate scale parameter.
 */
nixie_point_list_t nixie_tessellate_rounded_rect(nixie_arena_t *arena, double x, double y, double w, double h, double rx,
                                                  double ry);

/* Tessellates an ellipse (rx==ry => circle) into a closed polygon. */
nixie_point_list_t nixie_tessellate_ellipse(nixie_arena_t *arena, double cx, double cy, double rx, double ry);

/* Segment count per quarter turn for a circular arc of the given
 * (already device-pixel-space) radius -- shared by rounded-rect corners,
 * ellipses, and stroke join/cap discs. */
int nixie_arc_segments_for_radius(double radius);

#endif /* NIXIE_SVGRASTER_SVG_PATH_H */
