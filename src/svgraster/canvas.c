#include "canvas.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

nixie_canvas_t *nixie_canvas_create(nixie_arena_t *arena, int w, int h, nixie_rgb_t background, int has_background) {
    nixie_canvas_t *c = (nixie_canvas_t *)nixie_arena_alloc(arena, sizeof(nixie_canvas_t));
    c->w = w;
    c->h = h;
    size_t n = (size_t)w * (size_t)h * 4;
    c->pixels = (unsigned char *)nixie_arena_alloc_zeroed(arena, n);

    if (has_background) {
        for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
            c->pixels[i * 4 + 0] = background.r;
            c->pixels[i * 4 + 1] = background.g;
            c->pixels[i * 4 + 2] = background.b;
            c->pixels[i * 4 + 3] = 255;
        }
    }

    return c;
}

static unsigned char clamp_u8(double v) {
    if (v <= 0.0) return 0;
    if (v >= 255.0) return 255;
    return (unsigned char)(v + 0.5);
}

void nixie_canvas_blend_pixel(nixie_canvas_t *canvas, int x, int y, nixie_rgb_t color, double alpha) {
    if (x < 0 || y < 0 || x >= canvas->w || y >= canvas->h || alpha <= 0.0) {
        return;
    }
    if (alpha > 1.0) alpha = 1.0;

    unsigned char *p = canvas->pixels + ((size_t)y * (size_t)canvas->w + (size_t)x) * 4;
    double src_a = alpha;
    double dst_a = p[3] / 255.0;
    double out_a = src_a + dst_a * (1.0 - src_a);

    if (out_a <= 1e-9) {
        p[0] = 0;
        p[1] = 0;
        p[2] = 0;
        p[3] = 0;
        return;
    }

    double out_r = (color.r * src_a + p[0] * dst_a * (1.0 - src_a)) / out_a;
    double out_g = (color.g * src_a + p[1] * dst_a * (1.0 - src_a)) / out_a;
    double out_b = (color.b * src_a + p[2] * dst_a * (1.0 - src_a)) / out_a;

    p[0] = clamp_u8(out_r);
    p[1] = clamp_u8(out_g);
    p[2] = clamp_u8(out_b);
    p[3] = clamp_u8(out_a * 255.0);
}

typedef struct {
    double x;
    int winding;
} crossing_t;

static int cmp_crossing(const void *a, const void *b) {
    double xa = ((const crossing_t *)a)->x;
    double xb = ((const crossing_t *)b)->x;
    if (xa < xb) return -1;
    if (xa > xb) return 1;
    return 0;
}

void nixie_canvas_fill_polygons(nixie_canvas_t *canvas, const nixie_polyline_list_t *polys, nixie_rgb_t color, double opacity) {
    if (polys->count == 0 || opacity <= 0.0) {
        return;
    }

    double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30;
    int total_points = 0;
    for (int li = 0; li < polys->count; li++) {
        const nixie_point_list_t *pl = &polys->lines[li].points;
        total_points += pl->count;
        for (int i = 0; i < pl->count; i++) {
            double px = pl->points[i].x, py = pl->points[i].y;
            if (px < minx) minx = px;
            if (px > maxx) maxx = px;
            if (py < miny) miny = py;
            if (py > maxy) maxy = py;
        }
    }
    if (total_points < 3) {
        return;
    }

    int bbox_x0 = (int)floor(minx);
    int bbox_y0 = (int)floor(miny);
    int bbox_x1 = (int)ceil(maxx);
    int bbox_y1 = (int)ceil(maxy);
    if (bbox_x0 < 0) bbox_x0 = 0;
    if (bbox_y0 < 0) bbox_y0 = 0;
    if (bbox_x1 > canvas->w) bbox_x1 = canvas->w;
    if (bbox_y1 > canvas->h) bbox_y1 = canvas->h;
    int bbox_w = bbox_x1 - bbox_x0;
    int bbox_h = bbox_y1 - bbox_y0;
    if (bbox_w <= 0 || bbox_h <= 0) {
        return;
    }

    const int SS = 4;
    crossing_t *crossings = (crossing_t *)malloc(sizeof(crossing_t) * (size_t)total_points);
    unsigned short *row_counts = (unsigned short *)malloc(sizeof(unsigned short) * (size_t)bbox_w);
    if (crossings == NULL || row_counts == NULL) {
        free(crossings);
        free(row_counts);
        return;
    }

    for (int dy = 0; dy < bbox_h; dy++) {
        memset(row_counts, 0, sizeof(unsigned short) * (size_t)bbox_w);

        for (int sub = 0; sub < SS; sub++) {
            double y = (double)(bbox_y0 + dy) + ((double)sub + 0.5) / SS;
            int nc = 0;

            for (int li = 0; li < polys->count; li++) {
                const nixie_point_list_t *pl = &polys->lines[li].points;
                int n = pl->count;
                for (int i = 0; i < n; i++) {
                    nixie_point_t a = pl->points[i];
                    nixie_point_t b = pl->points[(i + 1) % n];
                    if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                        double t = (y - a.y) / (b.y - a.y);
                        crossings[nc].x = a.x + t * (b.x - a.x);
                        crossings[nc].winding = (b.y > a.y) ? 1 : -1;
                        nc++;
                    }
                }
            }

            if (nc == 0) continue;
            qsort(crossings, (size_t)nc, sizeof(crossing_t), cmp_crossing);

            int winding = 0;
            for (int k = 0; k < nc - 1; k++) {
                winding += crossings[k].winding;
                if (winding == 0) continue;

                int sc0 = (int)floor((crossings[k].x - bbox_x0) * SS);
                int sc1 = (int)ceil((crossings[k + 1].x - bbox_x0) * SS);
                if (sc0 < 0) sc0 = 0;
                if (sc1 > bbox_w * SS) sc1 = bbox_w * SS;
                for (int sc = sc0; sc < sc1; sc++) {
                    int dst_x = sc / SS;
                    if (dst_x >= 0 && dst_x < bbox_w) row_counts[dst_x]++;
                }
            }
        }

        for (int dx = 0; dx < bbox_w; dx++) {
            if (row_counts[dx] == 0) continue;
            double coverage = (double)row_counts[dx] / (double)(SS * SS);
            if (coverage > 1.0) coverage = 1.0;
            nixie_canvas_blend_pixel(canvas, bbox_x0 + dx, bbox_y0 + dy, color, coverage * opacity);
        }
    }

    free(crossings);
    free(row_counts);
}

static double point_dist(nixie_point_t a, nixie_point_t b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return sqrt(dx * dx + dy * dy);
}

static void fill_one_polygon(nixie_canvas_t *canvas, nixie_arena_t *arena, nixie_point_list_t points, nixie_rgb_t color,
                              double opacity) {
    nixie_polyline_list_t one = {0};
    nixie_polyline_t pl;
    pl.points = points;
    pl.closed = 1;
    nixie_polyline_list_push(arena, &one, pl);
    nixie_canvas_fill_polygons(canvas, &one, color, opacity);
}

static void stroke_run(nixie_canvas_t *canvas, nixie_arena_t *arena, const nixie_point_t *pts, int n, int closed,
                        double half_width, nixie_rgb_t color, double opacity) {
    if (n < 1) return;

    if (n == 1) {
        nixie_point_list_t disc = nixie_tessellate_ellipse(arena, pts[0].x, pts[0].y, half_width, half_width);
        fill_one_polygon(canvas, arena, disc, color, opacity);
        return;
    }

    int n_edges = closed ? n : n - 1;
    for (int i = 0; i < n_edges; i++) {
        nixie_point_t a = pts[i];
        nixie_point_t b = pts[(i + 1) % n];
        double len = point_dist(a, b);
        if (len < 1e-9) continue;

        double nx = -(b.y - a.y) / len * half_width;
        double ny = (b.x - a.x) / len * half_width;
        nixie_point_list_t quad = {0};
        nixie_point_t corners[4] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
        for (int k = 0; k < 4; k++) nixie_point_list_push(arena, &quad, corners[k]);
        fill_one_polygon(canvas, arena, quad, color, opacity);
    }

    /* Round join/cap disc at every vertex -- an accepted simplification:
     * real stroke widths here (0.75-1.5px) make miter vs. round joins
     * visually indistinguishable. */
    for (int i = 0; i < n; i++) {
        nixie_point_list_t disc = nixie_tessellate_ellipse(arena, pts[i].x, pts[i].y, half_width, half_width);
        fill_one_polygon(canvas, arena, disc, color, opacity);
    }
}

void nixie_canvas_stroke_polyline(nixie_canvas_t *canvas, nixie_arena_t *arena, const nixie_point_t *points, int count,
                                   int closed, double stroke_width, nixie_rgb_t color, double opacity, double dash_on,
                                   double dash_off) {
    if (count < 1 || stroke_width <= 0.0 || opacity <= 0.0) {
        return;
    }
    double half = stroke_width * 0.5;

    if (dash_on <= 0.0 || dash_off <= 0.0) {
        stroke_run(canvas, arena, points, count, closed, half, color, opacity);
        return;
    }
    if (count < 2) {
        return;
    }

    int n_edges = closed ? count : count - 1;
    double pattern_pos = 0.0;
    int on = 1;
    nixie_point_list_t run = {0};
    nixie_point_list_push(arena, &run, points[0]);

    for (int i = 0; i < n_edges; i++) {
        nixie_point_t a = points[i];
        nixie_point_t b = points[(i + 1) % count];
        double seg_len = point_dist(a, b);
        if (seg_len < 1e-9) continue;
        double s = 0.0;

        while (s < seg_len - 1e-9) {
            double pattern_len = on ? dash_on : dash_off;
            double limit = pattern_len - pattern_pos;
            double step = seg_len - s;
            if (limit < step) step = limit;
            if (step < 0.0) step = 0.0;
            s += step;
            pattern_pos += step;

            double t = s / seg_len;
            nixie_point_t p = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            if (on) {
                nixie_point_list_push(arena, &run, p);
            }

            if (pattern_pos >= pattern_len - 1e-9) {
                if (on && run.count >= 2) {
                    stroke_run(canvas, arena, run.points, run.count, 0, half, color, opacity);
                }
                on = !on;
                pattern_pos = 0.0;
                run.points = NULL;
                run.count = 0;
                run.cap = 0;
                if (on) {
                    nixie_point_list_push(arena, &run, p);
                }
            }
        }
    }

    if (on && run.count >= 2) {
        stroke_run(canvas, arena, run.points, run.count, 0, half, color, opacity);
    }
}
