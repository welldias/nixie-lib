#include "render_ascii.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../strbuf.h"
#include "../text_metrics.h"

/* ==========================================================================
 * Character grid (small local copy -- see class/render_ascii.c's note on
 * why these aren't factored into a shared module)
 * ========================================================================== */

typedef struct nixie_ascii_grid {
    uint32_t *cells;
    int width;
    int height;
} nixie_ascii_grid_t;

static nixie_ascii_grid_t *grid_create(nixie_arena_t *a, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    nixie_ascii_grid_t *g = (nixie_ascii_grid_t *)nixie_arena_alloc(a, sizeof(nixie_ascii_grid_t));
    g->width = w;
    g->height = h;
    g->cells = (uint32_t *)nixie_arena_alloc(a, (size_t)w * (size_t)h * sizeof(uint32_t));
    for (int i = 0; i < w * h; i++) g->cells[i] = ' ';
    return g;
}

static void grid_set(nixie_ascii_grid_t *g, int x, int y, uint32_t ch) {
    if (x < 0 || y < 0 || x >= g->width || y >= g->height) return;
    g->cells[(size_t)y * (size_t)g->width + (size_t)x] = ch;
}

static uint32_t grid_get(const nixie_ascii_grid_t *g, int x, int y) {
    if (x < 0 || y < 0 || x >= g->width || y >= g->height) return ' ';
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
            if (grid_get(g, x, y) != (uint32_t)' ') last_non_space = x;
        }
        for (int x = 0; x <= last_non_space; x++) {
            append_utf8(sb, grid_get(g, x, y));
        }
        if (y < g->height - 1) nixie_strbuf_append_char(sb, '\n');
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

static void write_text_n(nixie_ascii_grid_t *g, int col, int row, const char *text, size_t byte_len) {
    const char *q = text;
    const char *end = text + byte_len;
    while (q < end) {
        uint32_t cp;
        q += nixie_utf8_decode(q, &cp);
        grid_set(g, col, row, cp);
        col++;
    }
}

static void write_text(nixie_ascii_grid_t *g, int col, int row, const char *text) {
    write_text_n(g, col, row, text, strlen(text));
}

static const char *seq_block_type_name(nixie_seq_block_type_t type) {
    switch (type) {
        case NIXIE_SEQ_BLOCK_LOOP: return "loop";
        case NIXIE_SEQ_BLOCK_ALT: return "alt";
        case NIXIE_SEQ_BLOCK_OPT: return "opt";
        case NIXIE_SEQ_BLOCK_PAR: return "par";
        case NIXIE_SEQ_BLOCK_CRITICAL: return "critical";
        case NIXIE_SEQ_BLOCK_BREAK: return "break";
        case NIXIE_SEQ_BLOCK_RECT:
        default: return "rect";
    }
}

/* ==========================================================================
 * Multi-line label helpers (labels are already normalized by the parser:
 * <br> variants and literal "\n" become real '\n' characters)
 * ========================================================================== */

typedef struct line_span {
    const char *ptr;
    size_t byte_len;
    size_t char_len; /* UTF-8 codepoint count, for centering/width math */
} line_span_t;

static size_t split_lines(nixie_arena_t *arena, const char *s, line_span_t **out) {
    size_t count = 1;
    for (const char *p = s; *p != '\0'; p++) {
        if (*p == '\n') count++;
    }
    line_span_t *spans = (line_span_t *)nixie_arena_alloc(arena, count * sizeof(line_span_t));

    size_t idx = 0;
    const char *start = s;
    for (const char *p = s;; p++) {
        if (*p == '\n' || *p == '\0') {
            spans[idx].ptr = start;
            spans[idx].byte_len = (size_t)(p - start);
            spans[idx].char_len = utf8_len(nixie_arena_strndup(arena, start, (size_t)(p - start)));
            idx++;
            if (*p == '\0') break;
            start = p + 1;
        }
    }

    *out = spans;
    return count;
}

static size_t max_line_width(const char *s) {
    size_t max_len = 0, cur = 0;
    const char *p = s;
    for (;;) {
        if (*p == '\0') {
            if (cur > max_len) max_len = cur;
            break;
        }
        if (*p == '\n') {
            if (cur > max_len) max_len = cur;
            cur = 0;
            p++;
            continue;
        }
        uint32_t cp;
        p += nixie_utf8_decode(p, &cp);
        cur++;
    }
    return max_len;
}

static size_t line_count(const char *s) {
    size_t n = 1;
    for (const char *p = s; *p != '\0'; p++) {
        if (*p == '\n') n++;
    }
    return n;
}

/* ==========================================================================
 * Note positions (computed as a side effect of the main vertical-layout
 * pass, one call per timeline anchor)
 * ========================================================================== */

typedef struct note_pos {
    int x, y, width, height;
    const line_span_t *lines;
    size_t line_count;
} note_pos_t;

/* Positions every note whose after_index matches `after_index`, advancing
 * *cur_y_inout past each one in turn (mirroring ascii/sequence.ts's own
 * curY += 1; ...; curY += nHeight sequencing per note). Called once before
 * the main per-message loop for after_index == -1 (a note before the first
 * message) and once per message index inside it -- see
 * sequence/layout.c's position_notes_for() for why the -1 case needs
 * separate handling: ascii/sequence.ts has the identical gap (its own
 * notesByAfterIndex-equivalent linear scan is nested inside the
 * per-message loop, so a note keyed -1 is never positioned there either),
 * fixed the same way here for consistency between the SVG and ASCII
 * backends. */
static int position_notes_ascii(
    nixie_arena_t *arena, const nixie_seq_diagram_t *diagram, const int *ll_x, int after_index, int *cur_y_inout,
    note_pos_t *note_positions, size_t *note_count_out) {
    int any = 0;

    for (size_t ni = 0; ni < diagram->note_count; ni++) {
        const nixie_seq_note_t *note = &diagram->notes[ni];
        if (note->after_index != after_index) continue;
        any = 1;

        *cur_y_inout += 1;

        line_span_t *lines;
        size_t lc = split_lines(arena, note->text, &lines);
        size_t max_w = max_line_width(note->text);
        int n_width = (int)max_w + 4;
        int n_height = (int)lc + 2;

        size_t a_idx = note->actor_count > 0 ? (size_t)note->actor_indices[0] : 0;
        int nx;
        if (note->position == NIXIE_SEQ_NOTE_LEFT) {
            nx = ll_x[a_idx] - n_width - 1;
        } else if (note->position == NIXIE_SEQ_NOTE_RIGHT) {
            nx = ll_x[a_idx] + 2;
        } else if (note->actor_count >= 2) {
            size_t a_idx2 = (size_t)note->actor_indices[1];
            nx = (ll_x[a_idx] + ll_x[a_idx2]) / 2 - n_width / 2;
        } else {
            nx = ll_x[a_idx] - n_width / 2;
        }
        if (nx < 0) nx = 0;

        note_pos_t *np = &note_positions[(*note_count_out)++];
        np->x = nx;
        np->y = *cur_y_inout;
        np->width = n_width;
        np->height = n_height;
        np->lines = lines;
        np->line_count = lc;

        *cur_y_inout += n_height;
    }

    return any;
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

char *nixie_sequence_render_ascii(
    nixie_arena_t *arena, const nixie_seq_diagram_t *diagram, const nixie_sequence_ascii_options_t *opts) {
    static const nixie_sequence_ascii_options_t default_opts = {1};
    if (opts == NULL) opts = &default_opts;
    int use_ascii = !opts->use_unicode;

    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    if (diagram->actor_count == 0) {
        nixie_strbuf_append(&sb, "(empty sequence diagram)");
        return nixie_strbuf_release(&sb);
    }

    uint32_t H = use_ascii ? (uint32_t)'-' : 0x2500;
    uint32_t V = use_ascii ? (uint32_t)'|' : 0x2502;
    uint32_t TL = use_ascii ? (uint32_t)'+' : 0x250C;
    uint32_t TR = use_ascii ? (uint32_t)'+' : 0x2510;
    uint32_t BL = use_ascii ? (uint32_t)'+' : 0x2514;
    uint32_t BR = use_ascii ? (uint32_t)'+' : 0x2518;
    uint32_t JT = use_ascii ? (uint32_t)'+' : 0x252C;
    uint32_t JB = use_ascii ? (uint32_t)'+' : 0x2534;
    uint32_t JL = use_ascii ? (uint32_t)'+' : 0x251C;
    uint32_t JR = use_ascii ? (uint32_t)'+' : 0x2524;

    size_t actor_count = diagram->actor_count;

    /* --- Layout: lifeline X positions --- */

    int box_pad = 1;
    int *actor_box_w = (int *)nixie_arena_alloc(arena, actor_count * sizeof(int));
    int *half_box = (int *)nixie_arena_alloc(arena, actor_count * sizeof(int));
    int *actor_box_h = (int *)nixie_arena_alloc(arena, actor_count * sizeof(int));
    int actor_box_H = 3;
    for (size_t i = 0; i < actor_count; i++) {
        size_t max_w = max_line_width(diagram->actors[i].label);
        size_t lc = line_count(diagram->actors[i].label);
        actor_box_w[i] = (int)max_w + 2 * box_pad + 2;
        half_box[i] = (actor_box_w[i] + 1) / 2;
        actor_box_h[i] = (int)lc + 2;
        if (actor_box_h[i] > actor_box_H) actor_box_H = actor_box_h[i];
    }

    size_t gap_count = actor_count > 1 ? actor_count - 1 : 0;
    int *adj_max_width = gap_count > 0 ? (int *)nixie_arena_alloc_zeroed(arena, gap_count * sizeof(int)) : NULL;
    for (size_t mi = 0; mi < diagram->message_count; mi++) {
        const nixie_seq_message_t *m = &diagram->messages[mi];
        if (m->from_idx == m->to_idx) continue;
        int lo = m->from_idx < m->to_idx ? m->from_idx : m->to_idx;
        int hi = m->from_idx > m->to_idx ? m->from_idx : m->to_idx;
        int needed = (int)max_line_width(m->label) + 4;
        int num_gaps = hi - lo;
        int per_gap = (needed + num_gaps - 1) / num_gaps;
        for (int g = lo; g < hi; g++) {
            if (per_gap > adj_max_width[g]) adj_max_width[g] = per_gap;
        }
    }

    int *ll_x = (int *)nixie_arena_alloc(arena, actor_count * sizeof(int));
    ll_x[0] = half_box[0];
    for (size_t i = 1; i < actor_count; i++) {
        int gap = half_box[i - 1] + half_box[i] + 2;
        if (adj_max_width[i - 1] + 2 > gap) gap = adj_max_width[i - 1] + 2;
        if (gap < 10) gap = 10;
        ll_x[i] = ll_x[i - 1] + gap;
    }

    /* --- Layout: vertical positions for messages, blocks, dividers, notes --- */

    size_t message_count = diagram->message_count;
    int *msg_arrow_y = message_count > 0 ? (int *)nixie_arena_alloc(arena, message_count * sizeof(int)) : NULL;
    int *msg_label_y = message_count > 0 ? (int *)nixie_arena_alloc(arena, message_count * sizeof(int)) : NULL;

    int *block_start_y = diagram->block_count > 0 ? (int *)nixie_arena_alloc(arena, diagram->block_count * sizeof(int)) : NULL;
    int *block_end_y = diagram->block_count > 0 ? (int *)nixie_arena_alloc(arena, diagram->block_count * sizeof(int)) : NULL;
    int **div_y = diagram->block_count > 0 ? (int **)nixie_arena_alloc(arena, diagram->block_count * sizeof(int *)) : NULL;
    for (size_t b = 0; b < diagram->block_count; b++) {
        block_start_y[b] = -1;
        block_end_y[b] = -1;
        div_y[b] = diagram->blocks[b].divider_count > 0
                       ? (int *)nixie_arena_alloc(arena, diagram->blocks[b].divider_count * sizeof(int))
                       : NULL;
        for (size_t d = 0; d < diagram->blocks[b].divider_count; d++) div_y[b][d] = -1;
    }

    note_pos_t *note_positions =
        diagram->note_count > 0 ? (note_pos_t *)nixie_arena_alloc(arena, diagram->note_count * sizeof(note_pos_t)) : NULL;
    size_t note_pos_count = 0;

    int cur_y = actor_box_H;

    position_notes_ascii(arena, diagram, ll_x, -1, &cur_y, note_positions, &note_pos_count);

    for (size_t m = 0; m < message_count; m++) {
        for (size_t b = 0; b < diagram->block_count; b++) {
            if (diagram->blocks[b].start_index == (int)m) {
                cur_y += 2;
                block_start_y[b] = cur_y - 1;
            }
            for (size_t d = 0; d < diagram->blocks[b].divider_count; d++) {
                if (diagram->blocks[b].dividers[d].index == (int)m) {
                    cur_y += 1;
                    div_y[b][d] = cur_y;
                    cur_y += 1;
                }
            }
        }

        cur_y += 1; /* blank row before message */

        const nixie_seq_message_t *msg = &diagram->messages[m];
        int is_self = msg->from_idx == msg->to_idx;
        size_t msg_line_count = line_count(msg->label);

        if (is_self) {
            msg_label_y[m] = cur_y + 1;
            msg_arrow_y[m] = cur_y;
            cur_y += 2 + (int)msg_line_count;
        } else {
            msg_label_y[m] = cur_y;
            msg_arrow_y[m] = cur_y + (int)msg_line_count;
            cur_y += (int)msg_line_count + 1;
        }

        position_notes_ascii(arena, diagram, ll_x, (int)m, &cur_y, note_positions, &note_pos_count);

        for (size_t b = 0; b < diagram->block_count; b++) {
            if (diagram->blocks[b].end_index == (int)m) {
                cur_y += 1;
                block_end_y[b] = cur_y;
                cur_y += 1;
            }
        }
    }

    cur_y += 1;
    int footer_y = cur_y;
    int total_h = footer_y + actor_box_H;

    int last_ll = ll_x[actor_count - 1];
    int last_half = half_box[actor_count - 1];
    int total_w = last_ll + last_half + 2;

    for (size_t m = 0; m < message_count; m++) {
        const nixie_seq_message_t *msg = &diagram->messages[m];
        if (msg->from_idx == msg->to_idx) {
            int fi = msg->from_idx;
            int self_right = ll_x[fi] + 6 + 2 + (int)max_line_width(msg->label);
            if (self_right + 1 > total_w) total_w = self_right + 1;
        }
    }
    for (size_t i = 0; i < note_pos_count; i++) {
        if (note_positions[i].x + note_positions[i].width + 1 > total_w) total_w = note_positions[i].x + note_positions[i].width + 1;
    }

    /* mkCanvas(totalW, totalH-1) in the JS reference builds (totalW+1)
     * columns x totalH rows under inclusive 0..N indexing -- see that
     * project's canvas.ts. This grid uses counted (not inclusive) width
     * and height, so the equivalent allocation is (totalW+1) x totalH. */
    nixie_ascii_grid_t *grid = grid_create(arena, total_w + 1, total_h);

    /* --- Draw: lifelines --- */

    for (size_t i = 0; i < actor_count; i++) {
        int x = ll_x[i];
        for (int y = actor_box_H; y <= footer_y; y++) grid_set(grid, x, y, V);
    }

    /* --- Draw: actor header + footer boxes (drawn over lifelines) --- */

    for (size_t i = 0; i < actor_count; i++) {
        const char *label = diagram->actors[i].label;
        line_span_t *lines;
        size_t lc = split_lines(arena, label, &lines);
        size_t max_w = max_line_width(label);
        int w = (int)max_w + 2 * box_pad + 2;
        int h = (int)lc + 2;

        for (int pass = 0; pass < 2; pass++) {
            int top_y = pass == 0 ? 0 : footer_y;
            int cx = ll_x[i];
            int left = cx - w / 2;

            grid_set(grid, left, top_y, TL);
            for (int x = 1; x < w - 1; x++) grid_set(grid, left + x, top_y, H);
            grid_set(grid, left + w - 1, top_y, TR);

            for (size_t li = 0; li < lc; li++) {
                int row = top_y + 1 + (int)li;
                grid_set(grid, left, row, V);
                grid_set(grid, left + w - 1, row, V);
                int ls = left + 1 + box_pad + ((int)max_w - (int)lines[li].char_len) / 2;
                write_text_n(grid, ls, row, lines[li].ptr, lines[li].byte_len);
            }

            int bottom_y = top_y + h - 1;
            grid_set(grid, left, bottom_y, BL);
            for (int x = 1; x < w - 1; x++) grid_set(grid, left + x, bottom_y, H);
            grid_set(grid, left + w - 1, bottom_y, BR);

            if (!use_ascii) {
                grid_set(grid, cx, actor_box_H - 1, JT);
                grid_set(grid, cx, footer_y, JB);
            }
        }
    }

    /* --- Draw: messages --- */

    for (size_t m = 0; m < message_count; m++) {
        const nixie_seq_message_t *msg = &diagram->messages[m];
        int from_x = ll_x[(size_t)msg->from_idx];
        int to_x = ll_x[(size_t)msg->to_idx];
        int is_self = msg->from_idx == msg->to_idx;
        int is_dashed = msg->line_style == NIXIE_SEQ_DASHED;
        int is_filled = msg->arrow_head == NIXIE_SEQ_FILLED;
        uint32_t line_char = is_dashed ? (use_ascii ? (uint32_t)'.' : 0x254C) : H;

        if (is_self) {
            int y0 = msg_arrow_y[m];
            int loop_w = 4;

            grid_set(grid, from_x, y0, JL);
            for (int x = from_x + 1; x < from_x + loop_w; x++) grid_set(grid, x, y0, line_char);
            grid_set(grid, from_x + loop_w, y0, use_ascii ? (uint32_t)'+' : 0x2510);

            grid_set(grid, from_x + loop_w, y0 + 1, V);
            int label_x = from_x + loop_w + 2;
            write_text(grid, label_x, y0 + 1, msg->label);

            uint32_t arrow_char = is_filled ? (use_ascii ? (uint32_t)'<' : 0x25C0) : (use_ascii ? (uint32_t)'<' : 0x25C1);
            grid_set(grid, from_x, y0 + 2, arrow_char);
            for (int x = from_x + 1; x < from_x + loop_w; x++) grid_set(grid, x, y0 + 2, line_char);
            grid_set(grid, from_x + loop_w, y0 + 2, use_ascii ? (uint32_t)'+' : 0x2518);
        } else {
            int label_y = msg_label_y[m];
            int arrow_y = msg_arrow_y[m];
            int left_to_right = from_x < to_x;

            int mid_x = (from_x + to_x) / 2;
            line_span_t *lines;
            size_t lc = split_lines(arena, msg->label, &lines);
            for (size_t li = 0; li < lc; li++) {
                int label_start = mid_x - (int)lines[li].char_len / 2;
                int y = label_y + (int)li;
                write_text_n(grid, label_start, y, lines[li].ptr, lines[li].byte_len);
            }

            if (left_to_right) {
                for (int x = from_x + 1; x < to_x; x++) grid_set(grid, x, arrow_y, line_char);
                uint32_t ah = is_filled ? (use_ascii ? (uint32_t)'>' : 0x25B6) : (use_ascii ? (uint32_t)'>' : 0x25B7);
                grid_set(grid, to_x, arrow_y, ah);
            } else {
                for (int x = to_x + 1; x < from_x; x++) grid_set(grid, x, arrow_y, line_char);
                uint32_t ah = is_filled ? (use_ascii ? (uint32_t)'<' : 0x25C0) : (use_ascii ? (uint32_t)'<' : 0x25C1);
                grid_set(grid, to_x, arrow_y, ah);
            }
        }
    }

    /* --- Draw: blocks --- */

    for (size_t b = 0; b < diagram->block_count; b++) {
        const nixie_seq_block_t *block = &diagram->blocks[b];
        int top_y = block_start_y[b];
        int bot_y = block_end_y[b];
        if (top_y < 0 || bot_y < 0) continue;

        int min_lx = total_w, max_lx = 0;
        for (int mi = block->start_index; mi <= block->end_index; mi++) {
            if (mi < 0 || mi >= (int)message_count) continue;
            const nixie_seq_message_t *m = &diagram->messages[mi];
            int f = m->from_idx, t = m->to_idx;
            int lo = f < t ? f : t, hi = f > t ? f : t;
            if (ll_x[lo] < min_lx) min_lx = ll_x[lo];
            if (ll_x[hi] > max_lx) max_lx = ll_x[hi];
        }

        int b_left = min_lx - 4 < 0 ? 0 : min_lx - 4;
        int b_right = max_lx + 4 > total_w - 1 ? total_w - 1 : max_lx + 4;

        grid_set(grid, b_left, top_y, TL);
        for (int x = b_left + 1; x < b_right; x++) grid_set(grid, x, top_y, H);
        grid_set(grid, b_right, top_y, TR);

        nixie_strbuf_t hdr_sb;
        nixie_strbuf_init(&hdr_sb);
        nixie_strbuf_append(&hdr_sb, seq_block_type_name(block->type));
        if (block->label[0] != '\0') {
            nixie_strbuf_append(&hdr_sb, " [");
            nixie_strbuf_append(&hdr_sb, block->label);
            nixie_strbuf_append_char(&hdr_sb, ']');
        }
        const char *hdr_label = hdr_sb.data != NULL ? hdr_sb.data : "";
        line_span_t *hdr_lines;
        size_t hdr_lc = split_lines(arena, hdr_label, &hdr_lines);
        for (size_t li = 0; li < hdr_lc && top_y + (int)li < bot_y; li++) {
            write_text_n(grid, b_left + 1, top_y + (int)li, hdr_lines[li].ptr, hdr_lines[li].byte_len);
        }
        nixie_strbuf_free(&hdr_sb);

        grid_set(grid, b_left, bot_y, BL);
        for (int x = b_left + 1; x < b_right; x++) grid_set(grid, x, bot_y, H);
        grid_set(grid, b_right, bot_y, BR);

        for (int y = top_y + 1; y < bot_y; y++) {
            grid_set(grid, b_left, y, V);
            grid_set(grid, b_right, y, V);
        }

        for (size_t d = 0; d < block->divider_count; d++) {
            int dy = div_y[b][d];
            if (dy < 0) continue;
            uint32_t dash_char = use_ascii ? (uint32_t)'-' : 0x254C;
            grid_set(grid, b_left, dy, JL);
            for (int x = b_left + 1; x < b_right; x++) grid_set(grid, x, dy, dash_char);
            grid_set(grid, b_right, dy, JR);

            const char *d_label = block->dividers[d].label;
            if (d_label[0] != '\0') {
                nixie_strbuf_t d_sb;
                nixie_strbuf_init(&d_sb);
                nixie_strbuf_append_char(&d_sb, '[');
                nixie_strbuf_append(&d_sb, d_label);
                nixie_strbuf_append_char(&d_sb, ']');
                size_t written = 0;
                const char *q = d_sb.data;
                while (*q != '\0' && b_left + 1 + (int)written < b_right) {
                    uint32_t cp;
                    q += nixie_utf8_decode(q, &cp);
                    grid_set(grid, b_left + 1 + (int)written, dy, cp);
                    written++;
                }
                nixie_strbuf_free(&d_sb);
            }
        }
    }

    /* --- Draw: notes --- */

    for (size_t i = 0; i < note_pos_count; i++) {
        const note_pos_t *np = &note_positions[i];
        grid_set(grid, np->x, np->y, TL);
        for (int x = 1; x < np->width - 1; x++) grid_set(grid, np->x + x, np->y, H);
        grid_set(grid, np->x + np->width - 1, np->y, TR);

        for (size_t li = 0; li < np->line_count; li++) {
            int ly = np->y + 1 + (int)li;
            grid_set(grid, np->x, ly, V);
            grid_set(grid, np->x + np->width - 1, ly, V);
            write_text_n(grid, np->x + 2, ly, np->lines[li].ptr, np->lines[li].byte_len);
        }

        int by = np->y + np->height - 1;
        grid_set(grid, np->x, by, BL);
        for (int x = 1; x < np->width - 1; x++) grid_set(grid, np->x + x, by, H);
        grid_set(grid, np->x + np->width - 1, by, BR);
    }

    grid_write(&sb, grid);
    return nixie_strbuf_release(&sb);
}
