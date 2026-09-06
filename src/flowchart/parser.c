#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../strbuf.h"
#include "graph_build.h"

/* ==========================================================================
 * Small scanning helpers
 * ========================================================================== */

static int ieq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return 1;
}

static int is_id_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-';
}

/*
 * Scans the leading run of node-id characters ([\w-]+, as in
 * beautiful-mermaid/src/parser.ts's BARE_NODE_REGEX). A literal '-' is
 * ambiguous: mermaid allows hyphens inside ids (e.g. "us-east"), but every
 * arrow operator this parser recognizes also starts with '-' (-->, ---,
 * -.->, -.-). A naive greedy [\w-]+ scan would eat the arrow's leading
 * hyphen(s) whenever there's no space before it (e.g. "A-->B" would
 * otherwise scan the id as "A--", leaving ">B" -- which matches no arrow --
 * so the edge is silently dropped). To avoid that, a '-' only extends the id
 * when it is NOT the start of a "--" or "-." run, which covers every arrow
 * operator's lead-in.
 */
static size_t scan_id_len(const char *s) {
    size_t n = 0;
    for (;;) {
        char c = s[n];
        if (!is_id_char(c)) {
            break;
        }
        if (c == '-' && (s[n + 1] == '-' || s[n + 1] == '.')) {
            break;
        }
        n++;
    }
    return n;
}

static size_t skip_ws(const char *s) {
    size_t n = 0;
    while (s[n] == ' ' || s[n] == '\t') {
        n++;
    }
    return n;
}

/*
 * Strips surrounding double quotes, converts <br>/<br/>/<br /> (any case)
 * and literal "\n" escapes to real newlines. A simplified subset of
 * beautiful-mermaid/src/multiline-utils.ts's normalizeBrTags(): markdown
 * bold/italic/strikethrough and <sub>/<sup>/<small>/<mark> stripping are not
 * ported in this v1 slice.
 */
static char *normalize_label(nixie_arena_t *arena, const char *raw, size_t raw_len) {
    size_t start = 0, end = raw_len;
    if (raw_len >= 2 && raw[0] == '"' && raw[raw_len - 1] == '"') {
        start = 1;
        end   = raw_len - 1;
    }

    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    size_t i = start;
    while (i < end) {
        if (raw[i] == '<' && i + 2 < end && (raw[i + 1] == 'b' || raw[i + 1] == 'B') && (raw[i + 2] == 'r' || raw[i + 2] == 'R')) {
            size_t j = i + 3;
            if (j < end && raw[j] == ' ')
                j++;
            if (j < end && raw[j] == '/')
                j++;
            if (j < end && raw[j] == '>') {
                nixie_strbuf_append_char(&sb, '\n');
                i = j + 1;
                continue;
            }
        }

        if (raw[i] == '\\' && i + 1 < end && raw[i + 1] == 'n') {
            nixie_strbuf_append_char(&sb, '\n');
            i += 2;
            continue;
        }

        nixie_strbuf_append_char(&sb, raw[i]);
        i++;
    }

    char *result = nixie_arena_strdup(arena, sb.data != NULL ? sb.data : "");
    nixie_strbuf_free(&sb);
    return result;
}

/* ==========================================================================
 * Node shape delimiter patterns -- ordered from most specific to least, per
 * beautiful-mermaid/src/parser.ts's NODE_PATTERNS (multi-char delimiters
 * must be tried before single-char ones to avoid false matches).
 * ========================================================================== */

typedef struct {
    const char *open;
    const char *close;
    nixie_node_shape_t shape;
} node_pattern_t;

static const node_pattern_t NODE_PATTERNS[] = {
    { "(((", ")))", NIXIE_SHAPE_DOUBLECIRCLE  },
    { "([",  "])",  NIXIE_SHAPE_STADIUM       },
    { "((",  "))",  NIXIE_SHAPE_CIRCLE        },
    { "[[",  "]]",  NIXIE_SHAPE_SUBROUTINE    },
    { "[(",  ")]",  NIXIE_SHAPE_CYLINDER      },
    { "[/",  "\\]", NIXIE_SHAPE_TRAPEZOID     },
    { "[\\", "/]",  NIXIE_SHAPE_TRAPEZOID_ALT },
    { ">",   "]",   NIXIE_SHAPE_ASYMMETRIC    },
    { "{{",  "}}",  NIXIE_SHAPE_HEXAGON       },
    { "[",   "]",   NIXIE_SHAPE_RECTANGLE     },
    { "(",   ")",   NIXIE_SHAPE_ROUNDED       },
    { "{",   "}",   NIXIE_SHAPE_DIAMOND       },
};
#define NODE_PATTERN_COUNT (sizeof(NODE_PATTERNS) / sizeof(NODE_PATTERNS[0]))

/*
 * Consumes one node reference (bare id, or id + shape delimiters + label)
 * from the start of `text`. Also consumes an optional ":::className"
 * shorthand suffix (parsed and discarded -- classDef/class assignment is out
 * of scope for this v1 slice). Returns 0 if no node id is present at all.
 */
static int consume_node(nixie_arena_t *arena, nixie_mm_graph_t *g, const char *text, int *index_out, size_t *consumed_out) {
    size_t id_len = scan_id_len(text);
    if (id_len == 0) {
        return 0;
    }

    const char *after_id = text + id_len;
    size_t shape_len     = 0;
    int idx;

    const node_pattern_t *matched = NULL;
    const char *label_start       = NULL;
    size_t label_len              = 0;

    for (size_t pi = 0; pi < NODE_PATTERN_COUNT; pi++) {
        const node_pattern_t *pat = &NODE_PATTERNS[pi];
        size_t open_len           = strlen(pat->open);
        if (strncmp(after_id, pat->open, open_len) != 0) {
            continue;
        }
        const char *search_from = after_id + open_len;
        const char *close_ptr   = strstr(search_from, pat->close);
        if (close_ptr == NULL) {
            continue;
        }
        label_start = search_from;
        label_len   = (size_t)(close_ptr - search_from);
        shape_len   = open_len + label_len + strlen(pat->close);
        matched     = pat;
        break;
    }

    if (matched != NULL) {
        char *label = normalize_label(arena, label_start, label_len);
        idx         = nixie_mm_find_or_add_node(arena, g, text, id_len, label, matched->shape);
    } else {
        idx       = nixie_mm_find_or_add_node(arena, g, text, id_len, NULL, NIXIE_SHAPE_RECTANGLE);
        shape_len = 0;
    }

    size_t consumed = id_len + shape_len;

    const char *after_shape = text + consumed;
    if (strncmp(after_shape, ":::", 3) == 0) {
        size_t cn_len = scan_id_len(after_shape + 3);
        if (cn_len > 0) {
            consumed += 3 + cn_len;
        }
    }

    *index_out    = idx;
    *consumed_out = consumed;
    return 1;
}

typedef struct {
    int *ids;
    size_t count;
    size_t consumed;
} node_group_t;

/* Consumes one or more nodes separated by '&' (e.g. "A & B & C --> ..."). */
static int consume_node_group(nixie_arena_t *arena, nixie_mm_graph_t *g, const char *text, node_group_t *out) {
    int first_idx;
    size_t first_consumed;
    if (!consume_node(arena, g, text, &first_idx, &first_consumed)) {
        return 0;
    }

    size_t cap   = 4;
    int *ids     = (int *)nixie_arena_alloc(arena, cap * sizeof(int));
    size_t count = 0;
    ids[count++] = first_idx;

    size_t pos = first_consumed;
    for (;;) {
        size_t ws = skip_ws(text + pos);
        if (text[pos + ws] != '&') {
            break;
        }
        size_t after_amp = pos + ws + 1;
        after_amp += skip_ws(text + after_amp);

        int next_idx;
        size_t next_consumed;
        if (!consume_node(arena, g, text + after_amp, &next_idx, &next_consumed)) {
            break;
        }

        if (count == cap) {
            size_t new_cap = cap * 2;
            int *new_ids   = (int *)nixie_arena_alloc(arena, new_cap * sizeof(int));
            memcpy(new_ids, ids, count * sizeof(int));
            ids = new_ids;
            cap = new_cap;
        }
        ids[count++] = next_idx;
        pos          = after_amp + next_consumed;
    }

    out->ids      = ids;
    out->count    = count;
    out->consumed = pos;
    return 1;
}

/* ==========================================================================
 * Arrow parsing
 * ========================================================================== */

typedef struct {
    const char *op;
    nixie_edge_style_t style;
    int has_end;
} arrow_op_t;

static const arrow_op_t ARROW_OPS[] = {
    { "-->",  NIXIE_EDGE_SOLID,  1 },
    { "-.->", NIXIE_EDGE_DOTTED, 1 },
    { "==>",  NIXIE_EDGE_THICK,  1 },
    { "---",  NIXIE_EDGE_SOLID,  0 },
    { "-.-",  NIXIE_EDGE_DOTTED, 0 },
    { "===",  NIXIE_EDGE_THICK,  0 },
};
#define ARROW_OP_COUNT (sizeof(ARROW_OPS) / sizeof(ARROW_OPS[0]))

/* Matches "-->", "-.->", "==>", "---", "-.-", "===", optionally prefixed by
 * '<' (bidirectional) and optionally followed by "|label|". */
static int try_match_arrow(const char *text, nixie_arena_t *arena, int *has_arrow_start, nixie_edge_style_t *style, int *has_arrow_end, char **label_out, size_t *consumed_out) {
    const char *p   = text;
    int arrow_start = 0;
    if (*p == '<') {
        arrow_start = 1;
        p++;
    }

    for (size_t i = 0; i < ARROW_OP_COUNT; i++) {
        size_t oplen = strlen(ARROW_OPS[i].op);
        if (strncmp(p, ARROW_OPS[i].op, oplen) != 0) {
            continue;
        }

        const char *q = p + oplen;
        char *label   = NULL;
        if (*q == '|') {
            const char *close = strchr(q + 1, '|');
            if (close != NULL) {
                label = normalize_label(arena, q + 1, (size_t)(close - (q + 1)));
                q     = close + 1;
            }
        }

        *has_arrow_start = arrow_start;
        *style           = ARROW_OPS[i].style;
        *has_arrow_end   = ARROW_OPS[i].has_end;
        *label_out       = label;
        *consumed_out    = (size_t)(q - text);
        return 1;
    }

    return 0;
}

static const char *const TEXT_ARROW_OPEN_OPS[]  = { "--", "-.", "==" };
static const char *const TEXT_ARROW_CLOSE_OPS[] = { "-->", "---", ".->", "-.-", "==>", "===" };

/* Fallback for text-embedded label syntax: "-- Yes -->", "-. Maybe .->",
 * "== Sure ==>". Finds the shortest label (non-greedy, mirroring
 * beautiful-mermaid's TEXT_ARROW_REGEX) between whitespace-delimited open
 * and close operators. */
static int try_match_text_arrow(const char *text, nixie_arena_t *arena, int *has_arrow_start, nixie_edge_style_t *style, int *has_arrow_end, char **label_out, size_t *consumed_out) {
    const char *p   = text;
    int arrow_start = 0;
    if (*p == '<') {
        arrow_start = 1;
        p++;
    }

    for (size_t oi = 0; oi < 3; oi++) {
        const char *op = TEXT_ARROW_OPEN_OPS[oi];
        size_t oplen   = strlen(op);
        if (strncmp(p, op, oplen) != 0) {
            continue;
        }

        const char *after_open = p + oplen;
        size_t ws1             = skip_ws(after_open);
        if (ws1 == 0) {
            continue;
        }
        const char *label_start = after_open + ws1;
        size_t max_len          = strlen(label_start);

        for (size_t llen = 1; llen <= max_len; llen++) {
            const char *after_label = label_start + llen;
            size_t ws2              = skip_ws(after_label);
            if (ws2 == 0) {
                continue;
            }
            const char *close_pos = after_label + ws2;

            for (size_t ci = 0; ci < 6; ci++) {
                const char *close_op = TEXT_ARROW_CLOSE_OPS[ci];
                size_t clen          = strlen(close_op);
                if (strncmp(close_pos, close_op, clen) != 0) {
                    continue;
                }

                size_t trimmed_len = llen;
                while (trimmed_len > 0 && (label_start[trimmed_len - 1] == ' ' || label_start[trimmed_len - 1] == '\t')) {
                    trimmed_len--;
                }
                if (trimmed_len == 0) {
                    continue;
                }

                nixie_edge_style_t st;
                if (strcmp(op, "-.") == 0 || strcmp(close_op, ".->") == 0 || strcmp(close_op, "-.-") == 0) {
                    st = NIXIE_EDGE_DOTTED;
                } else if (strcmp(op, "==") == 0 || strcmp(close_op, "==>") == 0 || strcmp(close_op, "===") == 0) {
                    st = NIXIE_EDGE_THICK;
                } else {
                    st = NIXIE_EDGE_SOLID;
                }

                *has_arrow_start = arrow_start;
                *style           = st;
                *has_arrow_end   = close_op[clen - 1] == '>';
                *label_out       = normalize_label(arena, label_start, trimmed_len);
                *consumed_out    = (size_t)(close_pos + clen - text);
                return 1;
            }
        }
    }

    return 0;
}

/*
 * Parses a line containing node/edge definitions. Handles chaining
 * (A --> B --> C) and '&' parallel groups (A & B --> C & D), emitting the
 * Cartesian product of edges between consecutive groups.
 */
static void parse_edge_line(nixie_arena_t *arena, nixie_mm_graph_t *g, const char *line) {
    size_t pos = skip_ws(line);

    node_group_t first_group;
    if (!consume_node_group(arena, g, line + pos, &first_group)) {
        return;
    }
    pos += first_group.consumed;

    int *prev_ids     = first_group.ids;
    size_t prev_count = first_group.count;

    for (;;) {
        pos += skip_ws(line + pos);
        if (line[pos] == '\0') {
            break;
        }

        int has_arrow_start      = 0;
        nixie_edge_style_t style = NIXIE_EDGE_SOLID;
        int has_arrow_end        = 0;
        char *label              = NULL;
        size_t arrow_consumed    = 0;

        int matched = try_match_arrow(line + pos, arena, &has_arrow_start, &style, &has_arrow_end, &label, &arrow_consumed);
        if (!matched) {
            matched = try_match_text_arrow(line + pos, arena, &has_arrow_start, &style, &has_arrow_end, &label, &arrow_consumed);
        }
        if (!matched) {
            break;
        }

        pos += arrow_consumed;
        pos += skip_ws(line + pos);

        node_group_t next_group;
        if (!consume_node_group(arena, g, line + pos, &next_group)) {
            break;
        }
        pos += next_group.consumed;

        for (size_t si = 0; si < prev_count; si++) {
            for (size_t ti = 0; ti < next_group.count; ti++) {
                nixie_mm_ensure_edge_capacity(arena, g);
                nixie_mm_edge_t *e = &g->edges[g->edge_count++];
                e->source_idx      = prev_ids[si];
                e->target_idx      = next_group.ids[ti];
                e->label           = label;
                e->style           = style;
                e->has_arrow_start = has_arrow_start;
                e->has_arrow_end   = has_arrow_end;
            }
        }

        prev_ids   = next_group.ids;
        prev_count = next_group.count;
    }
}

/* ==========================================================================
 * Header + directive lines
 * ========================================================================== */

static int parse_header(const char *line, size_t len, nixie_direction_t *dir_out) {
    size_t kw_len;
    if (len >= 5 && ieq_n(line, "graph", 5)) {
        kw_len = 5;
    } else if (len >= 9 && ieq_n(line, "flowchart", 9)) {
        kw_len = 9;
    } else {
        return 0;
    }

    size_t i = kw_len;
    if (i >= len || (line[i] != ' ' && line[i] != '\t')) {
        return 0;
    }
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }

    size_t dir_start = i;
    while (i < len && line[i] != ' ' && line[i] != '\t') {
        i++;
    }
    size_t dir_len = i - dir_start;

    size_t j = i;
    while (j < len && (line[j] == ' ' || line[j] == '\t')) {
        j++;
    }
    if (j != len || dir_len != 2) {
        return 0;
    }

    const char *dtok = line + dir_start;
    if (ieq_n(dtok, "TD", 2) || ieq_n(dtok, "TB", 2)) {
        *dir_out = NIXIE_DIR_TD;
    } else if (ieq_n(dtok, "LR", 2)) {
        *dir_out = NIXIE_DIR_LR;
    } else if (ieq_n(dtok, "BT", 2)) {
        *dir_out = NIXIE_DIR_BT;
    } else if (ieq_n(dtok, "RL", 2)) {
        *dir_out = NIXIE_DIR_RL;
    } else {
        return 0;
    }

    return 1;
}

static int starts_with_kw(const char *line, const char *kw) {
    size_t len = strlen(kw);
    if (strncmp(line, kw, len) != 0) {
        return 0;
    }
    return line[len] == '\0' || line[len] == ' ' || line[len] == '\t';
}

/* Lines this v1 slice deliberately does not implement (subgraphs, classDef,
 * class assignment, style, linkStyle, direction override) are recognized and
 * skipped rather than mis-parsed as edges -- see model.h's note on reserved
 * fields for the future subgraph-nested layout work. */
static int is_skipped_directive(const char *line) {
    if (strcmp(line, "end") == 0)
        return 1;
    if (starts_with_kw(line, "subgraph"))
        return 1;
    if (starts_with_kw(line, "direction"))
        return 1;
    if (starts_with_kw(line, "classDef"))
        return 1;
    if (starts_with_kw(line, "class"))
        return 1;
    if (starts_with_kw(line, "style"))
        return 1;
    if (starts_with_kw(line, "linkStyle"))
        return 1;
    return 0;
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

nixie_parse_result_t nixie_flowchart_parse(nixie_arena_t *arena, const char *text) {
    nixie_parse_result_t result;
    result.graph            = NULL;
    result.error            = NIXIE_OK;
    result.error_message[0] = '\0';
    result.error_line       = -1;

    nixie_sig_lines_t sig = nixie_split_significant_lines(arena, text);

    if (sig.count == 0) {
        result.error = NIXIE_ERROR_EMPTY_INPUT;
        snprintf(result.error_message, sizeof(result.error_message), "Empty mermaid diagram");
        return result;
    }

    nixie_direction_t direction;
    if (!parse_header(sig.lines[0].content, strlen(sig.lines[0].content), &direction)) {
        result.error = NIXIE_ERROR_UNKNOWN_HEADER;
        snprintf(result.error_message, sizeof(result.error_message), "Invalid mermaid header: \"%s\". Expected \"graph TD\", \"flowchart LR\", etc.", sig.lines[0].content);
        result.error_line = sig.lines[0].line_no;
        return result;
    }

    nixie_mm_graph_t *graph = (nixie_mm_graph_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_mm_graph_t));
    graph->direction        = direction;
    graph->node_index       = nixie_strmap_create(arena, 64);

    for (size_t i = 1; i < sig.count; i++) {
        const char *line = sig.lines[i].content;
        if (is_skipped_directive(line)) {
            continue;
        }
        parse_edge_line(arena, graph, line);
    }

    result.graph = graph;
    return result;
}
