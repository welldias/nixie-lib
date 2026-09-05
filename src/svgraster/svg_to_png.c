#include "svg_to_png.h"

#include <math.h>
#include <string.h>

#include "../arena.h"
#include "canvas.h"
#include "png_encode.h"
#include "svg_ir.h"
#include "svg_parse.h"
#include "svg_path.h"
#include "text.h"

static nixie_png_result_t make_error(nixie_error_t err, const char *msg) {
    nixie_png_result_t r;
    r.data = NULL;
    r.size = 0;
    r.error = err;
    r.error_line = -1;
    if (msg != NULL) {
        strncpy(r.error_message, msg, sizeof(r.error_message) - 1);
        r.error_message[sizeof(r.error_message) - 1] = '\0';
    } else {
        r.error_message[0] = '\0';
    }
    return r;
}

static const nixie_svg_marker_def_t *find_marker(const nixie_svg_document_t *doc, const char *id) {
    for (int i = 0; i < doc->marker_count; i++) {
        if (strcmp(doc->markers[i].id, id) == 0) return &doc->markers[i];
    }
    return NULL;
}

static double angle_of(nixie_point_t a, nixie_point_t b) {
    return atan2(b.y - a.y, b.x - a.x);
}

/* Rotates the marker's own polygon (authored relative to its refX/refY
 * anchor) by `angle_rad` and translates it so that anchor lands on `at` --
 * matches orient="auto"/"auto-start-reverse", the only orient value
 * nixie's own generators ever use. */
static void draw_marker(nixie_canvas_t *canvas, nixie_arena_t *arena, const nixie_svg_marker_def_t *m, nixie_point_t at,
                        double angle_rad) {
    if (m->point_count < 3 || !m->has_fill) return;

    nixie_point_list_t poly = {0};
    double c = cos(angle_rad), s = sin(angle_rad);
    for (int i = 0; i < m->point_count; i++) {
        double lx = m->points[i].x - m->ref_x;
        double ly = m->points[i].y - m->ref_y;
        double rx = lx * c - ly * s;
        double ry = lx * s + ly * c;
        nixie_point_t p = {at.x + rx, at.y + ry};
        nixie_point_list_push(arena, &poly, p);
    }

    nixie_polyline_list_t one = {0};
    nixie_polyline_t pl;
    pl.points = poly;
    pl.closed = 1;
    nixie_polyline_list_push(arena, &one, pl);
    nixie_canvas_fill_polygons(canvas, &one, m->fill, 1.0);
}

static void draw_markers(nixie_canvas_t *canvas, nixie_arena_t *arena, const nixie_svg_document_t *doc,
                          const nixie_svg_shape_t *s, const nixie_point_t *pts, int count) {
    if (count < 2) return;
    if (s->marker_start.present) {
        const nixie_svg_marker_def_t *m = find_marker(doc, s->marker_start.id);
        if (m != NULL) {
            double angle = angle_of(pts[1], pts[0]); /* auto-start-reverse: reversed direction */
            draw_marker(canvas, arena, m, pts[0], angle);
        }
    }
    if (s->marker_end.present) {
        const nixie_svg_marker_def_t *m = find_marker(doc, s->marker_end.id);
        if (m != NULL) {
            double angle = angle_of(pts[count - 2], pts[count - 1]);
            draw_marker(canvas, arena, m, pts[count - 1], angle);
        }
    }
}

static void draw_fill_stroke(nixie_canvas_t *canvas, nixie_arena_t *arena, nixie_point_list_t *poly,
                              nixie_svg_paint_t paint) {
    if (poly->count < 3) return;

    if (paint.has_fill) {
        nixie_polyline_list_t one = {0};
        nixie_polyline_t pl;
        pl.points = *poly;
        pl.closed = 1;
        nixie_polyline_list_push(arena, &one, pl);
        nixie_canvas_fill_polygons(canvas, &one, paint.fill, paint.opacity);
    }
    if (paint.has_stroke) {
        nixie_canvas_stroke_polyline(canvas, arena, poly->points, poly->count, 1, paint.stroke_width, paint.stroke,
                                      paint.opacity, paint.dash_on, paint.dash_off);
    }
}

nixie_png_result_t nixie_svg_to_png_impl(const char *svg_text, const char *font_path, double scale) {
    if (scale <= 0.0) scale = 1.0;

    nixie_arena_t *arena = nixie_arena_create(0);
    if (arena == NULL) {
        return make_error(NIXIE_ERROR_OUT_OF_MEMORY, "failed to allocate rasterizer arena");
    }

    nixie_svg_parse_result_t parsed = nixie_svg_parse(arena, svg_text, scale);
    if (parsed.error != NIXIE_OK) {
        nixie_png_result_t r = make_error(parsed.error, parsed.error_message);
        r.error_line = parsed.error_line;
        nixie_arena_destroy(arena);
        return r;
    }

    nixie_svg_document_t *doc = parsed.doc;
    int canvas_w = (int)(doc->width + 0.5);
    int canvas_h = (int)(doc->height + 0.5);
    if (canvas_w <= 0 || canvas_h <= 0) {
        nixie_arena_destroy(arena);
        return make_error(NIXIE_ERROR_PARSE, "SVG canvas dimensions must be positive");
    }

    nixie_canvas_t *canvas = nixie_canvas_create(arena, canvas_w, canvas_h, doc->background, doc->has_background);

    int need_font = 0;
    for (int i = 0; i < doc->shape_count; i++) {
        if (doc->shapes[i].kind == NIXIE_SVG_SHAPE_TEXT) {
            need_font = 1;
            break;
        }
    }

    nixie_font_t font;
    font.file_data = NULL;
    font.stbtt_info = NULL;
    font.ok = 0;
    if (need_font) {
        if (font_path == NULL) {
            nixie_arena_destroy(arena);
            return make_error(NIXIE_ERROR_INVALID_ARGUMENT, "this SVG contains <text> elements but font_path is NULL");
        }
        font = nixie_font_load(font_path);
        if (!font.ok) {
            nixie_arena_destroy(arena);
            return make_error(NIXIE_ERROR_INVALID_ARGUMENT,
                               "failed to load font_path (not found or not a valid .ttf/.otf file)");
        }
    }

    for (int i = 0; i < doc->shape_count; i++) {
        const nixie_svg_shape_t *s = &doc->shapes[i];
        switch (s->kind) {
            case NIXIE_SVG_SHAPE_RECT: {
                nixie_point_list_t poly = nixie_tessellate_rounded_rect(arena, s->x, s->y, s->w, s->h, s->rx, s->ry);
                draw_fill_stroke(canvas, arena, &poly, s->paint);
                break;
            }
            case NIXIE_SVG_SHAPE_CIRCLE: {
                nixie_point_list_t poly = nixie_tessellate_ellipse(arena, s->cx, s->cy, s->r, s->r);
                draw_fill_stroke(canvas, arena, &poly, s->paint);
                break;
            }
            case NIXIE_SVG_SHAPE_ELLIPSE: {
                nixie_point_list_t poly = nixie_tessellate_ellipse(arena, s->cx, s->cy, s->rx, s->ry);
                draw_fill_stroke(canvas, arena, &poly, s->paint);
                break;
            }
            case NIXIE_SVG_SHAPE_LINE: {
                nixie_point_t pts[2] = {{s->x1, s->y1}, {s->x2, s->y2}};
                if (s->paint.has_stroke) {
                    nixie_canvas_stroke_polyline(canvas, arena, pts, 2, 0, s->paint.stroke_width, s->paint.stroke,
                                                  s->paint.opacity, s->paint.dash_on, s->paint.dash_off);
                }
                draw_markers(canvas, arena, doc, s, pts, 2);
                break;
            }
            case NIXIE_SVG_SHAPE_POLYLINE: {
                if (s->paint.has_stroke && s->point_count >= 2) {
                    nixie_canvas_stroke_polyline(canvas, arena, s->points, s->point_count, 0, s->paint.stroke_width,
                                                  s->paint.stroke, s->paint.opacity, s->paint.dash_on, s->paint.dash_off);
                }
                draw_markers(canvas, arena, doc, s, s->points, s->point_count);
                break;
            }
            case NIXIE_SVG_SHAPE_POLYGON: {
                if (s->point_count >= 3) {
                    nixie_point_list_t poly;
                    poly.points = s->points;
                    poly.count = s->point_count;
                    poly.cap = s->point_count;
                    draw_fill_stroke(canvas, arena, &poly, s->paint);
                }
                break;
            }
            case NIXIE_SVG_SHAPE_PATH: {
                nixie_polyline_list_t polys = nixie_flatten_path(arena, s->path_ops, s->path_op_count, 0.25);
                if (s->paint.has_fill && polys.count > 0) {
                    nixie_canvas_fill_polygons(canvas, &polys, s->paint.fill, s->paint.opacity);
                }
                if (s->paint.has_stroke) {
                    for (int li = 0; li < polys.count; li++) {
                        nixie_canvas_stroke_polyline(canvas, arena, polys.lines[li].points.points,
                                                      polys.lines[li].points.count, polys.lines[li].closed,
                                                      s->paint.stroke_width, s->paint.stroke, s->paint.opacity,
                                                      s->paint.dash_on, s->paint.dash_off);
                    }
                }
                break;
            }
            case NIXIE_SVG_SHAPE_TEXT:
                nixie_draw_text(canvas, &font, s);
                break;
        }
    }

    if (font.ok) {
        nixie_font_free(&font);
    }

    size_t png_size = 0;
    unsigned char *png_data = nixie_png_encode(canvas, &png_size);
    nixie_arena_destroy(arena);

    if (png_data == NULL) {
        return make_error(NIXIE_ERROR_OUT_OF_MEMORY, "PNG encoding failed");
    }

    nixie_png_result_t r;
    r.data = png_data;
    r.size = png_size;
    r.error = NIXIE_OK;
    r.error_message[0] = '\0';
    r.error_line = -1;
    return r;
}
