#include "svg_path.h"

#include <math.h>
#include <string.h>

#define NIXIE_PI 3.14159265358979323846

void nixie_point_list_push(nixie_arena_t *arena, nixie_point_list_t *list, nixie_point_t p) {
    if (list->count == list->cap) {
        int new_cap               = (list->cap == 0) ? 8 : list->cap * 2;
        nixie_point_t *new_points = (nixie_point_t *)nixie_arena_alloc(arena, sizeof(nixie_point_t) * (size_t)new_cap);
        if (list->count > 0) {
            memcpy(new_points, list->points, sizeof(nixie_point_t) * (size_t)list->count);
        }
        list->points = new_points;
        list->cap    = new_cap;
    }
    list->points[list->count++] = p;
}

void nixie_polyline_list_push(nixie_arena_t *arena, nixie_polyline_list_t *list, nixie_polyline_t line) {
    if (list->count == list->cap) {
        int new_cap                 = (list->cap == 0) ? 4 : list->cap * 2;
        nixie_polyline_t *new_lines = (nixie_polyline_t *)nixie_arena_alloc(arena, sizeof(nixie_polyline_t) * (size_t)new_cap);
        if (list->count > 0) {
            memcpy(new_lines, list->lines, sizeof(nixie_polyline_t) * (size_t)list->count);
        }
        list->lines = new_lines;
        list->cap   = new_cap;
    }
    list->lines[list->count++] = line;
}

/* Perpendicular distance of `c` from the chord p0-p3 (falls back to
 * distance from the chord midpoint when p0 and p3 nearly coincide, to
 * avoid dividing by ~0). */
static double dist_from_chord(nixie_point_t p0, nixie_point_t p3, nixie_point_t c) {
    double dx   = p3.x - p0.x;
    double dy   = p3.y - p0.y;
    double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) {
        double mx = c.x - (p0.x + p3.x) * 0.5;
        double my = c.y - (p0.y + p3.y) * 0.5;
        return sqrt(mx * mx + my * my);
    }
    double cross = fabs((c.x - p0.x) * dy - (c.y - p0.y) * dx);
    return cross / sqrt(len2);
}

void nixie_flatten_cubic(nixie_arena_t *arena, nixie_point_t p0, nixie_point_t c1, nixie_point_t c2, nixie_point_t p3, double tolerance, int depth, nixie_point_list_t *out) {
    if (depth >= 10 || (dist_from_chord(p0, p3, c1) + dist_from_chord(p0, p3, c2)) < tolerance) {
        nixie_point_list_push(arena, out, p3);
        return;
    }

    nixie_point_t p01   = { (p0.x + c1.x) * 0.5, (p0.y + c1.y) * 0.5 };
    nixie_point_t p12   = { (c1.x + c2.x) * 0.5, (c1.y + c2.y) * 0.5 };
    nixie_point_t p23   = { (c2.x + p3.x) * 0.5, (c2.y + p3.y) * 0.5 };
    nixie_point_t p012  = { (p01.x + p12.x) * 0.5, (p01.y + p12.y) * 0.5 };
    nixie_point_t p123  = { (p12.x + p23.x) * 0.5, (p12.y + p23.y) * 0.5 };
    nixie_point_t p0123 = { (p012.x + p123.x) * 0.5, (p012.y + p123.y) * 0.5 };

    nixie_flatten_cubic(arena, p0, p01, p012, p0123, tolerance, depth + 1, out);
    nixie_flatten_cubic(arena, p0123, p123, p23, p3, tolerance, depth + 1, out);
}

nixie_polyline_list_t nixie_flatten_path(nixie_arena_t *arena, const nixie_path_op_t *ops, int op_count, double tolerance) {
    nixie_polyline_list_t result = { 0 };
    nixie_point_list_t current   = { 0 };
    nixie_point_t start          = { 0.0, 0.0 };
    nixie_point_t cur            = { 0.0, 0.0 };
    int have_subpath             = 0;

    for (int i = 0; i < op_count; i++) {
        const nixie_path_op_t *op = &ops[i];
        switch (op->kind) {
        case NIXIE_PATHOP_MOVE:
            if (have_subpath && current.count > 0) {
                nixie_polyline_t line;
                line.points = current;
                line.closed = 0;
                nixie_polyline_list_push(arena, &result, line);
            }
            current.points = NULL;
            current.count  = 0;
            current.cap    = 0;
            nixie_point_list_push(arena, &current, op->p);
            start        = op->p;
            cur          = op->p;
            have_subpath = 1;
            break;
        case NIXIE_PATHOP_LINE:
            nixie_point_list_push(arena, &current, op->p);
            cur = op->p;
            break;
        case NIXIE_PATHOP_QUAD: {
            nixie_point_t c1 = { cur.x + (2.0 / 3.0) * (op->c1.x - cur.x), cur.y + (2.0 / 3.0) * (op->c1.y - cur.y) };
            nixie_point_t c2 = { op->p.x + (2.0 / 3.0) * (op->c1.x - op->p.x), op->p.y + (2.0 / 3.0) * (op->c1.y - op->p.y) };
            nixie_flatten_cubic(arena, cur, c1, c2, op->p, tolerance, 0, &current);
            cur = op->p;
            break;
        }
        case NIXIE_PATHOP_CUBIC:
            nixie_flatten_cubic(arena, cur, op->c1, op->c2, op->p, tolerance, 0, &current);
            cur = op->p;
            break;
        case NIXIE_PATHOP_CLOSE:
            if (have_subpath && current.count > 0) {
                nixie_polyline_t line;
                line.points = current;
                line.closed = 1;
                nixie_polyline_list_push(arena, &result, line);
            }
            current.points = NULL;
            current.count  = 0;
            current.cap    = 0;
            have_subpath   = 0;
            cur            = start;
            break;
        }
    }

    if (have_subpath && current.count > 0) {
        nixie_polyline_t line;
        line.points = current;
        line.closed = 0;
        nixie_polyline_list_push(arena, &result, line);
    }

    return result;
}

int nixie_arc_segments_for_radius(double radius) {
    int segs = (int)(radius * 0.5 + 0.5);
    if (segs < 4)
        segs = 4;
    if (segs > 24)
        segs = 24;
    return segs;
}

nixie_point_list_t nixie_tessellate_rounded_rect(nixie_arena_t *arena, double x, double y, double w, double h, double rx, double ry) {
    nixie_point_list_t out = { 0 };

    if (rx <= 0.0 || ry <= 0.0) {
        nixie_point_t corners[4] = {
            { x,     y     },
            { x + w, y     },
            { x + w, y + h },
            { x,     y + h }
        };
        for (int i = 0; i < 4; i++) {
            nixie_point_list_push(arena, &out, corners[i]);
        }
        return out;
    }

    if (rx > w * 0.5)
        rx = w * 0.5;
    if (ry > h * 0.5)
        ry = h * 0.5;

    int segs         = nixie_arc_segments_for_radius((rx + ry) * 0.5);
    double cx[4]     = { x + rx, x + w - rx, x + w - rx, x + rx };
    double cy[4]     = { y + ry, y + ry, y + h - ry, y + h - ry };
    double theta0[4] = { NIXIE_PI, 1.5 * NIXIE_PI, 0.0, 0.5 * NIXIE_PI };
    double theta1[4] = { 1.5 * NIXIE_PI, 2.0 * NIXIE_PI, 0.5 * NIXIE_PI, NIXIE_PI };

    for (int c = 0; c < 4; c++) {
        for (int i = 0; i <= segs; i++) {
            double t        = theta0[c] + (theta1[c] - theta0[c]) * ((double)i / (double)segs);
            nixie_point_t p = { cx[c] + rx * cos(t), cy[c] + ry * sin(t) };
            nixie_point_list_push(arena, &out, p);
        }
    }

    return out;
}

nixie_point_list_t nixie_tessellate_ellipse(nixie_arena_t *arena, double cx, double cy, double rx, double ry) {
    nixie_point_list_t out = { 0 };
    int segs_per_quarter   = nixie_arc_segments_for_radius((rx + ry) * 0.5);
    int total              = segs_per_quarter * 4;

    for (int i = 0; i < total; i++) {
        double t        = (2.0 * NIXIE_PI) * ((double)i / (double)total);
        nixie_point_t p = { cx + rx * cos(t), cy + ry * sin(t) };
        nixie_point_list_push(arena, &out, p);
    }

    return out;
}
