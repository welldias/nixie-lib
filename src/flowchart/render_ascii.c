#include "render_ascii.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../strbuf.h"
#include "../text_metrics.h"

#define ROW_GAP 3
#define COL_GAP 5
#define MARGIN 1

/* ==========================================================================
 * Character grid
 * ========================================================================== */

typedef struct nixie_ascii_grid {
    uint32_t *cells;
    int width;
    int height;
} nixie_ascii_grid_t;

static nixie_ascii_grid_t *grid_create(nixie_arena_t *a, int w, int h) {
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    nixie_ascii_grid_t *g = (nixie_ascii_grid_t *)nixie_arena_alloc(a, sizeof(nixie_ascii_grid_t));
    g->width              = w;
    g->height             = h;
    g->cells              = (uint32_t *)nixie_arena_alloc(a, (size_t)w * (size_t)h * sizeof(uint32_t));
    for (int i = 0; i < w * h; i++) {
        g->cells[i] = ' ';
    }
    return g;
}

static void grid_set(nixie_ascii_grid_t *g, int x, int y, uint32_t ch) {
    if (x < 0 || y < 0 || x >= g->width || y >= g->height)
        return;
    g->cells[(size_t)y * (size_t)g->width + (size_t)x] = ch;
}

static uint32_t grid_get(const nixie_ascii_grid_t *g, int x, int y) {
    if (x < 0 || y < 0 || x >= g->width || y >= g->height)
        return ' ';
    return g->cells[(size_t)y * (size_t)g->width + (size_t)x];
}

static void append_utf8(nixie_strbuf_t *sb, uint32_t cp) {
    char buf[4];
    if (cp < 0x80) {
        nixie_strbuf_append_char(sb, (char)cp);
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        nixie_strbuf_append_n(sb, buf, 2);
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        nixie_strbuf_append_n(sb, buf, 3);
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        nixie_strbuf_append_n(sb, buf, 4);
    }
}

static void grid_write(nixie_strbuf_t *sb, const nixie_ascii_grid_t *g) {
    for (int y = 0; y < g->height; y++) {
        int last_non_space = -1;
        for (int x = 0; x < g->width; x++) {
            if (grid_get(g, x, y) != (uint32_t)' ')
                last_non_space = x;
        }
        for (int x = 0; x <= last_non_space; x++) {
            append_utf8(sb, grid_get(g, x, y));
        }
        if (y < g->height - 1) {
            nixie_strbuf_append_char(sb, '\n');
        }
    }
}

/* ==========================================================================
 * Label measurement/drawing helpers
 * ========================================================================== */

static size_t utf8_len(const char *s, size_t byte_len) {
    size_t count = 0;
    size_t i     = 0;
    while (i < byte_len) {
        uint32_t cp;
        i += nixie_utf8_decode(s + i, &cp);
        count++;
    }
    return count;
}

static void measure_label_chars(const char *label, size_t *out_max_len, size_t *out_line_count) {
    size_t max_len = 0, count = 1;
    const char *line_start = label;
    for (const char *p = label;; p++) {
        if (*p == '\n' || *p == '\0') {
            size_t byte_len = (size_t)(p - line_start);
            size_t cl       = utf8_len(line_start, byte_len);
            if (cl > max_len)
                max_len = cl;
            if (*p == '\0')
                break;
            line_start = p + 1;
            count++;
        }
    }
    *out_max_len    = max_len;
    *out_line_count = count;
}

typedef struct {
    uint32_t tl, tr, bl, br, h, v;
} border_glyphs_t;

static border_glyphs_t borders_for(int rounded, int use_unicode) {
    border_glyphs_t g;
    if (use_unicode) {
        g.h = 0x2500;
        g.v = 0x2502;
        if (rounded) {
            g.tl = 0x256D;
            g.tr = 0x256E;
            g.bl = 0x2570;
            g.br = 0x256F;
        } else {
            g.tl = 0x250C;
            g.tr = 0x2510;
            g.bl = 0x2514;
            g.br = 0x2518;
        }
    } else {
        g.h  = '-';
        g.v  = '|';
        g.tl = '+';
        g.tr = '+';
        g.bl = '+';
        g.br = '+';
    }
    return g;
}

static void draw_box(nixie_ascii_grid_t *g, int x, int y, int w, int h, const char *label, int rounded, int use_unicode) {
    border_glyphs_t bg = borders_for(rounded, use_unicode);

    grid_set(g, x, y, bg.tl);
    grid_set(g, x + w - 1, y, bg.tr);
    grid_set(g, x, y + h - 1, bg.bl);
    grid_set(g, x + w - 1, y + h - 1, bg.br);
    for (int cx = x + 1; cx < x + w - 1; cx++) {
        grid_set(g, cx, y, bg.h);
        grid_set(g, cx, y + h - 1, bg.h);
    }
    for (int cy = y + 1; cy < y + h - 1; cy++) {
        grid_set(g, x, cy, bg.v);
        grid_set(g, x + w - 1, cy, bg.v);
    }

    if (label == NULL || label[0] == '\0') {
        return;
    }

    int inner_w            = w - 2;
    int line_no            = 0;
    const char *line_start = label;
    for (const char *p = label;; p++) {
        if (*p == '\n' || *p == '\0') {
            size_t byte_len = (size_t)(p - line_start);
            size_t char_len = utf8_len(line_start, byte_len);
            int pad         = (inner_w - (int)char_len) / 2;
            if (pad < 0)
                pad = 0;
            int row = y + 1 + line_no;

            const char *q        = line_start;
            const char *line_end = line_start + byte_len;
            int col              = x + 1 + pad;
            while (q < line_end) {
                uint32_t cp;
                size_t adv = nixie_utf8_decode(q, &cp);
                grid_set(g, col, row, cp);
                col++;
                q += adv;
            }

            line_no++;
            if (*p == '\0')
                break;
            line_start = p + 1;
        }
    }
}

static int is_rounded_shape(nixie_node_shape_t shape) {
    return shape == NIXIE_SHAPE_ROUNDED || shape == NIXIE_SHAPE_STADIUM || shape == NIXIE_SHAPE_CIRCLE || shape == NIXIE_SHAPE_DOUBLECIRCLE;
}

/* ==========================================================================
 * Edge line drawing
 * ========================================================================== */

static uint32_t glyph_h(int use_unicode) {
    return use_unicode ? (uint32_t)0x2500 : (uint32_t)'-';
}
static uint32_t glyph_v(int use_unicode) {
    return use_unicode ? (uint32_t)0x2502 : (uint32_t)'|';
}

static int row_is_clear(const nixie_ascii_grid_t *g, int row, int col_start, int len) {
    if (row < 0 || row >= g->height)
        return 0;
    for (int c = col_start; c < col_start + len; c++) {
        if (c < 0 || c >= g->width)
            return 0;
        if (grid_get(g, c, row) != (uint32_t)' ')
            return 0;
    }
    return 1;
}

static void draw_vline(nixie_ascii_grid_t *g, int col, int y1, int y2, int use_unicode) {
    int lo = y1 < y2 ? y1 : y2;
    int hi = y1 < y2 ? y2 : y1;
    for (int y = lo; y <= hi; y++) {
        grid_set(g, col, y, glyph_v(use_unicode));
    }
}

static void draw_hline(nixie_ascii_grid_t *g, int row, int x1, int x2, int use_unicode) {
    int lo = x1 < x2 ? x1 : x2;
    int hi = x1 < x2 ? x2 : x1;
    for (int x = lo; x <= hi; x++) {
        grid_set(g, x, row, glyph_h(use_unicode));
    }
}

typedef struct {
    int ex, ey; /* exit point (on the source box's boundary) */
    int nx, ny; /* entry point (on the target box's boundary) */
    char arrow_end;
    char arrow_start;
} ascii_edge_route_t;

/* Picks the exit/entry sides (mirroring layout_layered.c's pixel-space
 * nixie_lg_route_edge(), but against character-grid boxes) based on which
 * side of the source the target actually falls on -- this stays correct
 * for edges reversed during cycle-breaking, whose target can sit on either
 * side despite the diagram's overall direction. */
static ascii_edge_route_t compute_edge_route(int vertical_primary, int sx, int sy, int sw, int sh, int tx, int ty, int tw, int th) {
    ascii_edge_route_t r;
    int scx = sx + sw / 2, scy = sy + sh / 2;
    int tcx = tx + tw / 2, tcy = ty + th / 2;

    if (vertical_primary) {
        if (ty >= sy) {
            r.ex          = scx;
            r.ey          = sy + sh;
            r.nx          = tcx;
            r.ny          = ty - 1;
            r.arrow_end   = 'v';
            r.arrow_start = '^';
        } else {
            r.ex          = scx;
            r.ey          = sy - 1;
            r.nx          = tcx;
            r.ny          = ty + th;
            r.arrow_end   = '^';
            r.arrow_start = 'v';
        }
    } else {
        if (tx >= sx) {
            r.ex          = sx + sw;
            r.ey          = scy;
            r.nx          = tx - 1;
            r.ny          = tcy;
            r.arrow_end   = '>';
            r.arrow_start = '<';
        } else {
            r.ex          = sx - 1;
            r.ey          = scy;
            r.nx          = tx + tw;
            r.ny          = tcy;
            r.arrow_end   = '<';
            r.arrow_start = '>';
        }
    }

    return r;
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

char *nixie_flowchart_render_ascii(nixie_arena_t *arena, const nixie_positioned_flowchart_t *pf, const nixie_ascii_options_t *opts) {
    static const nixie_ascii_options_t default_opts = { 0 };
    if (opts == NULL) {
        opts = &default_opts;
    }
    int use_unicode = opts->use_unicode;

    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    if (pf->node_count == 0) {
        nixie_strbuf_append(&sb, "(empty flowchart)");
        return nixie_strbuf_release(&sb);
    }

    int vertical_primary = (pf->direction == NIXIE_DIR_TD || pf->direction == NIXIE_DIR_BT);
    size_t n             = pf->node_count;

    /* Per-node character box size, from label content (not scaled from the
     * pixel layout, to avoid truncating text -- see render_ascii.h). */
    int *box_w = (int *)nixie_arena_alloc(arena, n * sizeof(int));
    int *box_h = (int *)nixie_arena_alloc(arena, n * sizeof(int));
    for (size_t i = 0; i < n; i++) {
        size_t max_len = 0, line_count = 1;
        const char *label = pf->nodes[i].label;
        if (label != NULL && label[0] != '\0') {
            measure_label_chars(label, &max_len, &line_count);
        }
        int w    = (int)max_len + 4;
        int h    = (int)line_count + 2;
        box_w[i] = w < 5 ? 5 : w;
        box_h[i] = h < 3 ? 3 : h;

        /* state-start/state-end pseudostates carry an empty label (the SVG
         * renderer draws them as a dot/bullseye instead); give them a small
         * fixed box with a synthetic glyph in draw_box() below rather than
         * falling through to a plain empty box. */
        if (pf->nodes[i].shape == NIXIE_SHAPE_STATE_START || pf->nodes[i].shape == NIXIE_SHAPE_STATE_END) {
            box_w[i] = 3;
            box_h[i] = 3;
        }
    }

    /* Regroup nodes by layer (see positioned.h) into rows (vertical-primary
     * directions) or columns (horizontal-primary directions). */
    int max_layer = 0;
    for (size_t i = 0; i < n; i++) {
        if (pf->nodes[i].layer > max_layer)
            max_layer = pf->nodes[i].layer;
    }
    int layer_count = max_layer + 1;

    int *layer_sizes = (int *)nixie_arena_alloc_zeroed(arena, (size_t)layer_count * sizeof(int));
    for (size_t i = 0; i < n; i++) {
        layer_sizes[pf->nodes[i].layer]++;
    }

    int **layer_members = (int **)nixie_arena_alloc(arena, (size_t)layer_count * sizeof(int *));
    int *fill_cursor    = (int *)nixie_arena_alloc_zeroed(arena, (size_t)layer_count * sizeof(int));
    for (int l = 0; l < layer_count; l++) {
        layer_members[l] = layer_sizes[l] > 0 ? (int *)nixie_arena_alloc(arena, (size_t)layer_sizes[l] * sizeof(int)) : NULL;
    }
    for (size_t i = 0; i < n; i++) {
        int l                              = pf->nodes[i].layer;
        layer_members[l][fill_cursor[l]++] = (int)i;
    }

    /* Sort each layer's members by cross-axis pixel coordinate ascending
     * (insertion sort; layers are small). */
    for (int l = 0; l < layer_count; l++) {
        int count = layer_sizes[l];
        for (int a = 1; a < count; a++) {
            int key          = layer_members[l][a];
            double key_cross = vertical_primary ? pf->nodes[key].x : pf->nodes[key].y;
            int b            = a - 1;
            while (b >= 0) {
                double b_cross = vertical_primary ? pf->nodes[layer_members[l][b]].x : pf->nodes[layer_members[l][b]].y;
                if (b_cross <= key_cross)
                    break;
                layer_members[l][b + 1] = layer_members[l][b];
                b--;
            }
            layer_members[l][b + 1] = key;
        }
    }

    /* Order the (non-empty) layers by their mean primary pixel coordinate,
     * so BT/RL's coordinate-assignment flip still yields correct visual
     * top-to-bottom / left-to-right reading order. */
    typedef struct {
        int layer;
        double mean_primary;
    } layer_order_entry_t;

    int non_empty = 0;
    for (int l = 0; l < layer_count; l++) {
        if (layer_sizes[l] > 0)
            non_empty++;
    }

    layer_order_entry_t *order = (layer_order_entry_t *)nixie_arena_alloc(arena, (size_t)non_empty * sizeof(layer_order_entry_t));
    int oc                     = 0;
    for (int l = 0; l < layer_count; l++) {
        if (layer_sizes[l] == 0)
            continue;
        double sum = 0.0;
        for (int a = 0; a < layer_sizes[l]; a++) {
            int idx = layer_members[l][a];
            sum += vertical_primary ? pf->nodes[idx].y : pf->nodes[idx].x;
        }
        order[oc].layer        = l;
        order[oc].mean_primary = sum / layer_sizes[l];
        oc++;
    }
    for (int a = 1; a < non_empty; a++) {
        layer_order_entry_t key = order[a];
        int b                   = a - 1;
        while (b >= 0 && order[b].mean_primary > key.mean_primary) {
            order[b + 1] = order[b];
            b--;
        }
        order[b + 1] = key;
    }

    /* Assign character-grid positions. */
    int *grid_x = (int *)nixie_arena_alloc(arena, n * sizeof(int));
    int *grid_y = (int *)nixie_arena_alloc(arena, n * sizeof(int));

    int primary_cursor = 0;
    for (int oi = 0; oi < non_empty; oi++) {
        int l     = order[oi].layer;
        int count = layer_sizes[l];

        int group_extent = 0;
        for (int a = 0; a < count; a++) {
            int idx    = layer_members[l][a];
            int extent = vertical_primary ? box_h[idx] : box_w[idx];
            if (extent > group_extent)
                group_extent = extent;
        }

        int cross_cursor = 0;
        for (int a = 0; a < count; a++) {
            int idx = layer_members[l][a];
            if (vertical_primary) {
                grid_x[idx] = cross_cursor;
                grid_y[idx] = primary_cursor;
                cross_cursor += box_w[idx] + COL_GAP;
            } else {
                grid_y[idx] = cross_cursor;
                grid_x[idx] = primary_cursor;
                cross_cursor += box_h[idx] + ROW_GAP;
            }
        }

        primary_cursor += group_extent + (vertical_primary ? ROW_GAP : COL_GAP);
    }

    /* Apply a uniform margin so nothing touches the canvas edge. */
    int canvas_w = 0, canvas_h = 0;
    for (size_t i = 0; i < n; i++) {
        grid_x[i] += MARGIN;
        grid_y[i] += MARGIN;
        int right  = grid_x[i] + box_w[i];
        int bottom = grid_y[i] + box_h[i];
        if (right > canvas_w)
            canvas_w = right;
        if (bottom > canvas_h)
            canvas_h = bottom;
    }
    canvas_w += MARGIN;
    canvas_h += MARGIN;

    /* Edges that don't connect adjacent layers (a genuine layer-skip, or a
     * back edge that a cycle forced nixie_lg_break_cycles() to reverse
     * during layering) cannot use the direct exit/elbow/entry routing
     * below: a straight run between their exit and entry points would cut
     * straight through every node and edge sitting in the layers between
     * them. Each such edge instead gets a dedicated channel that detours
     * around the margin (right, for vertical-primary directions; bottom,
     * for horizontal-primary ones) -- mirroring the general idea of
     * beautiful-mermaid's margin-routed cross-hierarchy edges, simplified
     * to a single fixed side per edge rather than an alternating one. */
    int *edge_channel   = pf->edge_count > 0 ? (int *)nixie_arena_alloc(arena, pf->edge_count * sizeof(int)) : NULL;
    int long_edge_count = 0;
    for (size_t i = 0; i < pf->edge_count; i++) {
        const nixie_pf_edge_t *edge = &pf->edges[i];
        int s = edge->source_idx, t = edge->target_idx;
        if (s == t) {
            edge_channel[i] = -1;
            continue;
        }
        int layer_diff = pf->nodes[t].layer - pf->nodes[s].layer;
        if (layer_diff < 0)
            layer_diff = -layer_diff;
        edge_channel[i] = (layer_diff != 1) ? long_edge_count++ : -1;
    }

    int content_canvas_w = canvas_w, content_canvas_h = canvas_h;
    if (vertical_primary) {
        canvas_w += long_edge_count * 2;
    } else {
        canvas_h += long_edge_count * 2;
    }

    nixie_ascii_grid_t *grid = grid_create(arena, canvas_w, canvas_h);

    /* Node boxes are drawn first here -- the opposite order from the SVG
     * renderer's (edges behind nodes there). Edge exit/entry points always
     * sit one cell outside a box by construction, so under normal layouts
     * this makes no visual difference; what it buys is that the label
     * placement pass below (which nudges onto whatever grid cells are
     * still blank) can see real box footprints and never nudges a label
     * onto a cell a box border will occupy, which would otherwise silently
     * erase that label once the box was drawn. */
    for (size_t i = 0; i < n; i++) {
        nixie_node_shape_t shape = pf->nodes[i].shape;
        const char *label        = pf->nodes[i].label;
        int rounded              = is_rounded_shape(shape);

        if (shape == NIXIE_SHAPE_STATE_START) {
            label   = use_unicode ? "\xE2\x97\x8F" /* U+25CF BLACK CIRCLE */ : "*";
            rounded = 1;
        } else if (shape == NIXIE_SHAPE_STATE_END) {
            /* Single-character glyphs only: the 3x3 pseudostate box has a
             * 1-column-wide interior, so anything longer would spill past
             * the right border into whatever sits next to it. */
            label   = use_unicode ? "\xE2\x97\x89" /* U+25C9 FISHEYE */ : "O";
            rounded = 1;
        }

        draw_box(grid, grid_x[i], grid_y[i], box_w[i], box_h[i], label, rounded, use_unicode);
    }

    /* Edge lines are drawn in one full pass, then labels in a second pass:
     * a later sibling edge's line/jog can otherwise sweep back over an
     * earlier sibling's already-written label (both fan out from the same
     * source exit point), so no label may be written until every edge's
     * line is settled. */
    for (size_t i = 0; i < pf->edge_count; i++) {
        const nixie_pf_edge_t *edge = &pf->edges[i];
        int s = edge->source_idx, t = edge->target_idx;
        if (s == t) {
            continue; /* self-loops are not supported by the ASCII v1 slice */
        }

        if (edge_channel[i] >= 0) {
            int channel = edge_channel[i];
            if (vertical_primary) {
                int ex = grid_x[s] + box_w[s], ey = grid_y[s] + box_h[s] / 2;
                int nx = grid_x[t] + box_w[t], ny = grid_y[t] + box_h[t] / 2;
                int side_col = content_canvas_w + channel * 2;
                draw_hline(grid, ey, ex, side_col, use_unicode);
                draw_vline(grid, side_col, ey, ny, use_unicode);
                draw_hline(grid, ny, side_col, nx, use_unicode);
                grid_set(grid, side_col, ey, '+');
                grid_set(grid, side_col, ny, '+');
                if (edge->has_arrow_end)
                    grid_set(grid, nx, ny, '<');
                if (edge->has_arrow_start)
                    grid_set(grid, ex, ey, '>');
            } else {
                int ex = grid_x[s] + box_w[s] / 2, ey = grid_y[s] + box_h[s];
                int nx = grid_x[t] + box_w[t] / 2, ny = grid_y[t] + box_h[t];
                int side_row = content_canvas_h + channel * 2;
                draw_vline(grid, ex, ey, side_row, use_unicode);
                draw_hline(grid, side_row, ex, nx, use_unicode);
                draw_vline(grid, nx, side_row, ny, use_unicode);
                grid_set(grid, ex, side_row, '+');
                grid_set(grid, nx, side_row, '+');
                if (edge->has_arrow_end)
                    grid_set(grid, nx, ny, '^');
                if (edge->has_arrow_start)
                    grid_set(grid, ex, ey, 'v');
            }
            continue;
        }

        ascii_edge_route_t route = compute_edge_route(vertical_primary, grid_x[s], grid_y[s], box_w[s], box_h[s], grid_x[t], grid_y[t], box_w[t], box_h[t]);
        int ex = route.ex, ey = route.ey, nx = route.nx, ny = route.ny;

        if (vertical_primary) {
            if (ex == nx) {
                draw_vline(grid, ex, ey, ny, use_unicode);
            } else {
                int mid = (ey + ny) / 2;
                draw_vline(grid, ex, ey, mid, use_unicode);
                draw_hline(grid, mid, ex, nx, use_unicode);
                draw_vline(grid, nx, mid, ny, use_unicode);
                grid_set(grid, ex, mid, '+');
                grid_set(grid, nx, mid, '+');
            }
        } else {
            if (ey == ny) {
                draw_hline(grid, ey, ex, nx, use_unicode);
            } else {
                int mid = (ex + nx) / 2;
                draw_hline(grid, ey, ex, mid, use_unicode);
                draw_vline(grid, mid, ey, ny, use_unicode);
                draw_hline(grid, ny, mid, nx, use_unicode);
                grid_set(grid, mid, ey, '+');
                grid_set(grid, mid, ny, '+');
            }
        }

        if (edge->has_arrow_end)
            grid_set(grid, nx, ny, (uint32_t)route.arrow_end);
        if (edge->has_arrow_start)
            grid_set(grid, ex, ey, (uint32_t)route.arrow_start);
    }

    for (size_t i = 0; i < pf->edge_count; i++) {
        const nixie_pf_edge_t *edge = &pf->edges[i];
        int s = edge->source_idx, t = edge->target_idx;
        if (s == t || edge->label == NULL || edge->label[0] == '\0') {
            continue;
        }

        int label_row, label_col;
        if (edge_channel[i] >= 0) {
            int channel = edge_channel[i];
            if (vertical_primary) {
                int ey = grid_y[s] + box_h[s] / 2, ny = grid_y[t] + box_h[t] / 2;
                label_row = (ey + ny) / 2;
                label_col = content_canvas_w + channel * 2 + 1;
            } else {
                int ex = grid_x[s] + box_w[s] / 2, nx = grid_x[t] + box_w[t] / 2;
                label_col = (ex + nx) / 2;
                label_row = content_canvas_h + channel * 2 + 1;
            }
        } else {
            ascii_edge_route_t route = compute_edge_route(vertical_primary, grid_x[s], grid_y[s], box_w[s], box_h[s], grid_x[t], grid_y[t], box_w[t], box_h[t]);
            int ex = route.ex, ey = route.ey, nx = route.nx, ny = route.ny;

            /* Place the label on the segment between the two corners (or,
             * for a straight 2-point route, just off to the side of the
             * line): the varying coordinate between fan-out siblings
             * anchors the position so sibling labels don't collide (see
             * compute_edge_route()'s callers -- exit points are shared per
             * source, entry points are not). */
            if (vertical_primary) {
                /* Biased 1/3 of the way from exit to entry (not the exact
                 * midpoint): two edges connecting the very same node pair
                 * in opposite directions swap which node is "source" and
                 * which is "target", so ex/nx and ey/ny both end up as the
                 * same two values either way -- an exact midpoint gives
                 * both edges the identical anchor. Biasing toward the exit
                 * side breaks that symmetry (ey itself differs between the
                 * two directions) while still landing within the segment. */
                label_row = ey + (ny - ey) / 3;
                label_col = (ex == nx) ? ex + 1 : (ex + nx) / 2;
            } else {
                label_col = ex + (nx - ex) / 3;
                label_row = (ey == ny) ? ey - 1 : (ey + ny) / 2;
            }
        }

        size_t first_line_len = 0;
        while (edge->label[first_line_len] != '\0' && edge->label[first_line_len] != '\n') {
            first_line_len++;
        }

        /* The heuristics above can still coincide -- most notably, two
         * edges connecting the same node pair in opposite directions (e.g.
         * a forward transition and the back-edge that closes a small
         * cycle) compute an identical (ex+nx)/2 column, since that sum is
         * symmetric regardless of which node is "source" -- so nudge onto
         * the nearest row whose cells are still blank rather than risk one
         * label overwriting another. */
        int label_char_len             = (int)utf8_len(edge->label, first_line_len);
        static const int row_offsets[] = { 0, 1, -1, 2, -2, 3, -3, 4, -4 };
        int chosen_row                 = label_row;
        for (size_t oi = 0; oi < sizeof(row_offsets) / sizeof(row_offsets[0]); oi++) {
            int candidate = label_row + row_offsets[oi];
            if (row_is_clear(grid, candidate, label_col, label_char_len)) {
                chosen_row = candidate;
                break;
            }
        }
        label_row = chosen_row;

        /* Nudge one cell right when the label would otherwise sit flush
         * against an arrowhead/line glyph with no separating space (e.g. an
         * arrow pointing straight at the label's first character). */
        if (grid_get(grid, label_col - 1, label_row) != (uint32_t)' ') {
            label_col++;
        }

        const char *q        = edge->label;
        const char *line_end = edge->label + first_line_len;
        int col              = label_col;
        while (q < line_end) {
            uint32_t cp;
            size_t adv = nixie_utf8_decode(q, &cp);
            grid_set(grid, col, label_row, cp);
            col++;
            q += adv;
        }
    }

    grid_write(&sb, grid);
    return nixie_strbuf_release(&sb);
}
