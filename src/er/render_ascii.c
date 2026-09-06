#include "render_ascii.h"

#include <stdint.h>
#include <string.h>

#include "../strbuf.h"
#include "../text_metrics.h"
#include "layout.h"

#define ROW_GAP 3
#define COL_GAP 5
#define MARGIN 1

static const char DIVIDER_MARKER_STORAGE = '\0';
#define DIVIDER_MARKER (&DIVIDER_MARKER_STORAGE)

/* ==========================================================================
 * Character grid (small local copy -- see flowchart/render_ascii.c's note
 * on why these aren't factored into a shared module)
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
    for (int i = 0; i < w * h; i++)
        g->cells[i] = ' ';
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
        if (y < g->height - 1)
            nixie_strbuf_append_char(sb, '\n');
    }
}

static size_t utf8_len(const char *s) {
    size_t count = 0;
    while (*s != '\0') {
        uint32_t cp;
        s += nixie_utf8_decode(s, &cp);
        count++;
    }
    return count;
}

static void write_text(nixie_ascii_grid_t *g, int col, int row, const char *text) {
    const char *q = text;
    while (*q != '\0') {
        uint32_t cp;
        q += nixie_utf8_decode(q, &cp);
        grid_set(g, col, row, cp);
        col++;
    }
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

/* ==========================================================================
 * Entity box content + sizing
 * ========================================================================== */

typedef struct {
    const char **lines;
    size_t line_count;
} entity_box_content_t;

static void push_line(nixie_arena_t *arena, const char ***lines, size_t *count, size_t *cap, const char *line) {
    if (*count == *cap) {
        size_t new_cap         = *cap == 0 ? 8 : *cap * 2;
        const char **new_lines = (const char **)nixie_arena_alloc(arena, new_cap * sizeof(char *));
        if (*count > 0)
            memcpy(new_lines, *lines, *count * sizeof(char *));
        *lines = new_lines;
        *cap   = new_cap;
    }
    (*lines)[(*count)++] = line;
}

static void push_multiline(nixie_arena_t *arena, const char ***lines, size_t *count, size_t *cap, const char *text) {
    const char *start = text;
    for (const char *p = text;; p++) {
        if (*p == '\n' || *p == '\0') {
            push_line(arena, lines, count, cap, nixie_arena_strndup(arena, start, (size_t)(p - start)));
            if (*p == '\0')
                break;
            start = p + 1;
        }
    }
}

static entity_box_content_t build_entity_box_content(nixie_arena_t *arena, const nixie_pe_node_t *entity) {
    const char **lines = NULL;
    size_t count = 0, cap = 0;

    push_multiline(arena, &lines, &count, &cap, entity->label);
    push_line(arena, &lines, &count, &cap, DIVIDER_MARKER);
    if (entity->attribute_count == 0) {
        push_line(arena, &lines, &count, &cap, "(no attributes)");
    } else {
        for (size_t i = 0; i < entity->attribute_count; i++) {
            push_line(arena, &lines, &count, &cap, nixie_er_attribute_to_string(arena, &entity->attributes[i]));
        }
    }

    entity_box_content_t result;
    result.lines      = lines;
    result.line_count = count;
    return result;
}

static void measure_box_content(const entity_box_content_t *content, size_t *out_max_len) {
    size_t max_len = 0;
    for (size_t i = 0; i < content->line_count; i++) {
        if (content->lines[i] == DIVIDER_MARKER)
            continue;
        size_t l = utf8_len(content->lines[i]);
        if (l > max_len)
            max_len = l;
    }
    *out_max_len = max_len;
}

static void draw_entity_box(nixie_ascii_grid_t *g, int x, int y, int w, int h, const entity_box_content_t *content, int use_unicode) {
    uint32_t h_glyph   = use_unicode ? (uint32_t)0x2500 : (uint32_t)'-';
    uint32_t v_glyph   = use_unicode ? (uint32_t)0x2502 : (uint32_t)'|';
    uint32_t tl        = use_unicode ? (uint32_t)0x250C : (uint32_t)'+';
    uint32_t tr        = use_unicode ? (uint32_t)0x2510 : (uint32_t)'+';
    uint32_t bl        = use_unicode ? (uint32_t)0x2514 : (uint32_t)'+';
    uint32_t br        = use_unicode ? (uint32_t)0x2518 : (uint32_t)'+';
    uint32_t divider_l = use_unicode ? (uint32_t)0x251C : (uint32_t)'+';
    uint32_t divider_r = use_unicode ? (uint32_t)0x2524 : (uint32_t)'+';

    grid_set(g, x, y, tl);
    grid_set(g, x + w - 1, y, tr);
    grid_set(g, x, y + h - 1, bl);
    grid_set(g, x + w - 1, y + h - 1, br);
    for (int cx = x + 1; cx < x + w - 1; cx++) {
        grid_set(g, cx, y, h_glyph);
        grid_set(g, cx, y + h - 1, h_glyph);
    }
    for (int cy = y + 1; cy < y + h - 1; cy++) {
        grid_set(g, x, cy, v_glyph);
        grid_set(g, x + w - 1, cy, v_glyph);
    }

    for (size_t i = 0; i < content->line_count; i++) {
        int row = y + 1 + (int)i;
        if (content->lines[i] == DIVIDER_MARKER) {
            grid_set(g, x, row, divider_l);
            grid_set(g, x + w - 1, row, divider_r);
            for (int cx = x + 1; cx < x + w - 1; cx++)
                grid_set(g, cx, row, h_glyph);
            continue;
        }
        write_text(g, x + 1, row, content->lines[i]);
    }
}

/* ==========================================================================
 * Crow's-foot marker glyphs -- ported conceptually from
 * beautiful-mermaid/src/ascii/er-diagram.ts's getCrowsFootChars(), using
 * plain box-drawing T-junctions instead of the reference's heavier
 * double-line variants (a cosmetic simplification only).
 * ========================================================================== */

static void write_crows_foot(nixie_ascii_grid_t *g, int col, int row, nixie_er_cardinality_t card, int use_unicode, int is_right) {
    uint32_t one        = use_unicode ? (uint32_t)0x2502 : (uint32_t)'|';
    uint32_t circle     = use_unicode ? (uint32_t)0x25CB : (uint32_t)'o';
    uint32_t many_right = use_unicode ? (uint32_t)0x251C : (uint32_t)'<';
    uint32_t many_left  = use_unicode ? (uint32_t)0x2524 : (uint32_t)'>';

    switch (card) {
    case NIXIE_ER_ONE:
        grid_set(g, col, row, one);
        break;
    case NIXIE_ER_ZERO_ONE:
        if (is_right) {
            grid_set(g, col, row, circle);
            grid_set(g, col + 1, row, one);
        } else {
            grid_set(g, col - 1, row, one);
            grid_set(g, col, row, circle);
        }
        break;
    case NIXIE_ER_MANY:
        grid_set(g, col, row, is_right ? many_right : many_left);
        break;
    case NIXIE_ER_ZERO_MANY:
        if (is_right) {
            grid_set(g, col, row, circle);
            grid_set(g, col + 1, row, many_right);
        } else {
            grid_set(g, col - 1, row, many_left);
            grid_set(g, col, row, circle);
        }
        break;
    }
}

/* ==========================================================================
 * Edge routing (same simplified elbow logic as class/render_ascii.c, always
 * horizontal since ER diagrams are laid out left-to-right)
 * ========================================================================== */

typedef struct {
    int ex, ey, nx, ny;
} er_edge_route_t;

static er_edge_route_t compute_route(int sx, int sy, int sw, int sh, int tx, int ty, int tw, int th) {
    er_edge_route_t r;
    int scy = sy + sh / 2;
    int tcy = ty + th / 2;
    if (tx >= sx) {
        r.ex = sx + sw;
        r.ey = scy;
        r.nx = tx - 1;
        r.ny = tcy;
    } else {
        r.ex = sx - 1;
        r.ey = scy;
        r.nx = tx + tw;
        r.ny = tcy;
    }
    return r;
}

static void draw_vline(nixie_ascii_grid_t *g, int col, int y1, int y2, int use_unicode) {
    uint32_t glyph = use_unicode ? (uint32_t)0x2502 : (uint32_t)'|';
    int lo = y1 < y2 ? y1 : y2, hi = y1 < y2 ? y2 : y1;
    for (int y = lo; y <= hi; y++)
        grid_set(g, col, y, glyph);
}

static void draw_hline(nixie_ascii_grid_t *g, int row, int x1, int x2, int use_unicode) {
    uint32_t glyph = use_unicode ? (uint32_t)0x2500 : (uint32_t)'-';
    int lo = x1 < x2 ? x1 : x2, hi = x1 < x2 ? x2 : x1;
    for (int x = lo; x <= hi; x++)
        grid_set(g, x, row, glyph);
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

char *nixie_er_render_ascii(nixie_arena_t *arena, const nixie_positioned_er_diagram_t *pcd, const nixie_er_ascii_options_t *opts) {
    static const nixie_er_ascii_options_t default_opts = { 0 };
    if (opts == NULL)
        opts = &default_opts;
    int use_unicode = opts->use_unicode;

    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    if (pcd->node_count == 0) {
        nixie_strbuf_append(&sb, "(empty ER diagram)");
        return nixie_strbuf_release(&sb);
    }

    size_t n                       = pcd->node_count;
    entity_box_content_t *contents = (entity_box_content_t *)nixie_arena_alloc(arena, n * sizeof(entity_box_content_t));
    int *box_w                     = (int *)nixie_arena_alloc(arena, n * sizeof(int));
    int *box_h                     = (int *)nixie_arena_alloc(arena, n * sizeof(int));

    for (size_t i = 0; i < n; i++) {
        contents[i] = build_entity_box_content(arena, &pcd->nodes[i]);
        size_t max_len;
        measure_box_content(&contents[i], &max_len);
        int w    = (int)max_len + 4;
        box_w[i] = w < 10 ? 10 : w;
        box_h[i] = (int)contents[i].line_count + 2;
    }

    /* Regroup by layer into columns (ER is always laid out left-to-right). */
    int max_layer = 0;
    for (size_t i = 0; i < n; i++) {
        if (pcd->nodes[i].layer > max_layer)
            max_layer = pcd->nodes[i].layer;
    }
    int layer_count = max_layer + 1;

    int *layer_sizes = (int *)nixie_arena_alloc_zeroed(arena, (size_t)layer_count * sizeof(int));
    for (size_t i = 0; i < n; i++)
        layer_sizes[pcd->nodes[i].layer]++;

    int **layer_members = (int **)nixie_arena_alloc(arena, (size_t)layer_count * sizeof(int *));
    int *fill_cursor    = (int *)nixie_arena_alloc_zeroed(arena, (size_t)layer_count * sizeof(int));
    for (int l = 0; l < layer_count; l++) {
        layer_members[l] = layer_sizes[l] > 0 ? (int *)nixie_arena_alloc(arena, (size_t)layer_sizes[l] * sizeof(int)) : NULL;
    }
    for (size_t i = 0; i < n; i++) {
        int l                              = pcd->nodes[i].layer;
        layer_members[l][fill_cursor[l]++] = (int)i;
    }

    /* Sort within each layer by cross-axis (y) pixel coordinate ascending. */
    for (int l = 0; l < layer_count; l++) {
        int count = layer_sizes[l];
        for (int a = 1; a < count; a++) {
            int key          = layer_members[l][a];
            double key_cross = pcd->nodes[key].y;
            int b            = a - 1;
            while (b >= 0 && pcd->nodes[layer_members[l][b]].y > key_cross) {
                layer_members[l][b + 1] = layer_members[l][b];
                b--;
            }
            layer_members[l][b + 1] = key;
        }
    }

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
        for (int a = 0; a < layer_sizes[l]; a++)
            sum += pcd->nodes[layer_members[l][a]].x;
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

    int *grid_x = (int *)nixie_arena_alloc(arena, n * sizeof(int));
    int *grid_y = (int *)nixie_arena_alloc(arena, n * sizeof(int));

    int primary_cursor = 0;
    for (int oi = 0; oi < non_empty; oi++) {
        int l     = order[oi].layer;
        int count = layer_sizes[l];

        int group_extent = 0;
        for (int a = 0; a < count; a++) {
            int idx = layer_members[l][a];
            if (box_w[idx] > group_extent)
                group_extent = box_w[idx];
        }

        int cross_cursor = 0;
        for (int a = 0; a < count; a++) {
            int idx     = layer_members[l][a];
            grid_y[idx] = cross_cursor;
            grid_x[idx] = primary_cursor;
            cross_cursor += box_h[idx] + ROW_GAP;
        }

        primary_cursor += group_extent + COL_GAP;
    }

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

    nixie_ascii_grid_t *grid = grid_create(arena, canvas_w, canvas_h);

    for (size_t i = 0; i < n; i++) {
        draw_entity_box(grid, grid_x[i], grid_y[i], box_w[i], box_h[i], &contents[i], use_unicode);
    }

    for (size_t i = 0; i < pcd->relationship_count; i++) {
        const nixie_pe_relationship_t *rel = &pcd->relationships[i];
        int s = rel->entity1_idx, t = rel->entity2_idx;
        if (s == t)
            continue;

        er_edge_route_t route = compute_route(grid_x[s], grid_y[s], box_w[s], box_h[s], grid_x[t], grid_y[t], box_w[t], box_h[t]);
        int ex = route.ex, ey = route.ey, nx = route.nx, ny = route.ny;

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

        /* entity1's marker faces right (toward the gap it exits into);
         * entity2's marker faces left (toward the gap it enters from). */
        int e1_is_right = nx >= ex;
        write_crows_foot(grid, ex, ey, rel->cardinality1, use_unicode, e1_is_right);
        write_crows_foot(grid, nx, ny, rel->cardinality2, use_unicode, !e1_is_right);

        if (rel->label != NULL && rel->label[0] != '\0') {
            int label_col = ex + (nx - ex) / 3;
            int label_row = (ey == ny) ? ey - 1 : (ey + ny) / 2;

            int label_len                  = (int)utf8_len(rel->label);
            static const int row_offsets[] = { 0, 1, -1, 2, -2, 3, -3 };
            int chosen_row                 = label_row;
            for (size_t oi = 0; oi < sizeof(row_offsets) / sizeof(row_offsets[0]); oi++) {
                int candidate = label_row + row_offsets[oi];
                if (row_is_clear(grid, candidate, label_col, label_len)) {
                    chosen_row = candidate;
                    break;
                }
            }
            label_row = chosen_row;
            if (grid_get(grid, label_col - 1, label_row) != (uint32_t)' ')
                label_col++;

            write_text(grid, label_col, label_row, rel->label);
        }
    }

    grid_write(&sb, grid);
    return nixie_strbuf_release(&sb);
}
