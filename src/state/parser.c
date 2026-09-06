#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../flowchart/graph_build.h"
#include "../strbuf.h"

/* ==========================================================================
 * Small scanning helpers -- deliberately local copies of
 * flowchart/parser.c's own primitives (ieq_n/is_id_char/scan_id_len/
 * skip_ws/normalize_label): the project plan only extracted the
 * node/edge-array and line-splitting logic shared via graph_build.h, not
 * these tiny char-level scanners, to avoid over-fragmenting a handful of
 * self-contained lines each parser can read on its own.
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

/* Same "--" lookahead guard as flowchart/parser.c's scan_id_len(): a state
 * id ending right before "-->" (the only transition operator here) must not
 * swallow the arrow's leading hyphen when there's no separating space. */
static size_t scan_id_len(const char *s) {
    size_t n = 0;
    for (;;) {
        char c = s[n];
        if (!is_id_char(c)) {
            break;
        }
        if (c == '-' && s[n + 1] == '-') {
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

static int starts_with_kw(const char *line, const char *kw) {
    size_t len = strlen(kw);
    if (strncmp(line, kw, len) != 0) {
        return 0;
    }
    return line[len] == '\0' || line[len] == ' ' || line[len] == '\t';
}

/* Strips surrounding double quotes, converts <br>/<br/>/<br /> (any case)
 * and literal "\n" escapes to real newlines -- same simplified subset of
 * normalizeBrTags() flowchart/parser.c uses. */
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
 * Header
 * ========================================================================== */

static int is_state_header(const char *line) {
    size_t len = strlen(line);
    if (len == 12 && ieq_n(line, "stateDiagram", 12)) {
        return 1;
    }
    if (len == 15 && ieq_n(line, "stateDiagram-v2", 15)) {
        return 1;
    }
    return 0;
}

/* ==========================================================================
 * Composite states -- recognized only enough to skip them (flattened; see
 * parser.h's doc comment and the project plan).
 * ========================================================================== */

static int ends_with_brace(const char *line) {
    size_t len = strlen(line);
    return len > 0 && line[len - 1] == '{';
}

/* ==========================================================================
 * `direction XX`
 * ========================================================================== */

static int parse_direction_token(const char *tok, size_t len, nixie_direction_t *out) {
    if (len != 2) {
        return 0;
    }
    if (ieq_n(tok, "TD", 2) || ieq_n(tok, "TB", 2)) {
        *out = NIXIE_DIR_TD;
    } else if (ieq_n(tok, "LR", 2)) {
        *out = NIXIE_DIR_LR;
    } else if (ieq_n(tok, "BT", 2)) {
        *out = NIXIE_DIR_BT;
    } else if (ieq_n(tok, "RL", 2)) {
        *out = NIXIE_DIR_RL;
    } else {
        return 0;
    }
    return 1;
}

static int try_direction_line(const char *line, nixie_direction_t *out) {
    if (!starts_with_kw(line, "direction")) {
        return 0;
    }
    size_t i = strlen("direction");
    i += skip_ws(line + i);
    size_t dir_start = i;
    while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t') {
        i++;
    }
    size_t dir_len = i - dir_start;
    i += skip_ws(line + i);
    if (line[i] != '\0') {
        return 0; /* trailing garbage after the direction token */
    }
    return parse_direction_token(line + dir_start, dir_len, out);
}

/* ==========================================================================
 * `state "Label" as id` (no trailing brace -- the brace form is a composite
 * and already skipped by ends_with_brace() before this is tried)
 * ========================================================================== */

static int try_state_alias(nixie_arena_t *arena, const char *line, const char **id_out, size_t *id_len_out, char **label_out) {
    if (!starts_with_kw(line, "state")) {
        return 0;
    }
    size_t i = strlen("state");
    i += skip_ws(line + i);
    if (line[i] != '"') {
        return 0;
    }

    const char *label_start = line + i + 1;
    const char *close       = strchr(label_start, '"');
    if (close == NULL) {
        return 0;
    }
    size_t label_len = (size_t)(close - label_start);

    size_t j = (size_t)(close - line) + 1;
    j += skip_ws(line + j);
    if (!(line[j] == 'a' && line[j + 1] == 's' && (line[j + 2] == ' ' || line[j + 2] == '\t'))) {
        return 0;
    }
    j += 2;
    j += skip_ws(line + j);

    size_t id_len = scan_id_len(line + j);
    if (id_len == 0 || line[j + id_len] != '\0') {
        return 0; /* no id, or trailing garbage after it */
    }

    *id_out     = line + j;
    *id_len_out = id_len;
    *label_out  = normalize_label(arena, label_start, label_len);
    return 1;
}

/* ==========================================================================
 * Transitions: `id --> id2`, `[*] --> id`, `id --> [*]`, optionally
 * `: label`. Only the "-->" operator exists in state diagrams (no arrow
 * style variety like the flowchart parser's).
 * ========================================================================== */

static char *make_pseudo_id(nixie_arena_t *arena, const char *prefix, int count) {
    char buf[32];
    if (count > 1) {
        snprintf(buf, sizeof(buf), "%s%d", prefix, count);
    } else {
        snprintf(buf, sizeof(buf), "%s", prefix);
    }
    return nixie_arena_strdup(arena, buf);
}

static int try_transition(nixie_arena_t *arena, nixie_mm_graph_t *g, const char *line, int *start_count, int *end_count) {
    const char *p = line;

    int src_is_start      = 0;
    const char *src_start = p;
    size_t src_len        = 0;
    if (strncmp(p, "[*]", 3) == 0) {
        src_is_start = 1;
        p += 3;
    } else {
        src_len = scan_id_len(p);
        if (src_len == 0) {
            return 0;
        }
        p += src_len;
    }

    p += skip_ws(p);
    if (strncmp(p, "-->", 3) != 0) {
        return 0;
    }
    p += 3;
    p += skip_ws(p);

    int dst_is_end        = 0;
    const char *dst_start = p;
    size_t dst_len        = 0;
    if (strncmp(p, "[*]", 3) == 0) {
        dst_is_end = 1;
        p += 3;
    } else {
        dst_len = scan_id_len(p);
        if (dst_len == 0) {
            return 0;
        }
        p += dst_len;
    }

    p += skip_ws(p);
    char *label = NULL;
    if (*p == ':') {
        p++;
        p += skip_ws(p);
        size_t rest_len = strlen(p);
        label           = normalize_label(arena, p, rest_len);
        p += rest_len;
    }

    if (*p != '\0') {
        return 0; /* trailing garbage: not a valid transition line */
    }

    int source_idx, target_idx;

    if (src_is_start) {
        (*start_count)++;
        char *id   = make_pseudo_id(arena, "_start", *start_count);
        source_idx = nixie_mm_find_or_add_node(arena, g, id, strlen(id), nixie_arena_strdup(arena, ""), NIXIE_SHAPE_STATE_START);
    } else {
        source_idx = nixie_mm_find_or_add_node(arena, g, src_start, src_len, NULL, NIXIE_SHAPE_ROUNDED);
    }

    if (dst_is_end) {
        (*end_count)++;
        char *id   = make_pseudo_id(arena, "_end", *end_count);
        target_idx = nixie_mm_find_or_add_node(arena, g, id, strlen(id), nixie_arena_strdup(arena, ""), NIXIE_SHAPE_STATE_END);
    } else {
        target_idx = nixie_mm_find_or_add_node(arena, g, dst_start, dst_len, NULL, NIXIE_SHAPE_ROUNDED);
    }

    nixie_mm_ensure_edge_capacity(arena, g);
    nixie_mm_edge_t *e = &g->edges[g->edge_count++];
    e->source_idx      = source_idx;
    e->target_idx      = target_idx;
    e->label           = label;
    e->style           = NIXIE_EDGE_SOLID;
    e->has_arrow_start = 0;
    e->has_arrow_end   = 1;

    return 1;
}

/* ==========================================================================
 * State description: `id : Description`
 * ========================================================================== */

static int try_state_description(nixie_arena_t *arena, nixie_mm_graph_t *g, const char *line) {
    size_t id_len = scan_id_len(line);
    if (id_len == 0) {
        return 0;
    }

    const char *p = line + id_len;
    p += skip_ws(p);
    if (*p != ':') {
        return 0;
    }
    p++;
    p += skip_ws(p);

    size_t desc_len = strlen(p);
    if (desc_len == 0) {
        return 0;
    }

    char *label = normalize_label(arena, p, desc_len);
    nixie_mm_find_or_add_node(arena, g, line, id_len, label, NIXIE_SHAPE_ROUNDED);
    return 1;
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

nixie_parse_result_t nixie_state_parse(nixie_arena_t *arena, const char *text) {
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

    if (!is_state_header(sig.lines[0].content)) {
        result.error = NIXIE_ERROR_UNKNOWN_HEADER;
        snprintf(result.error_message, sizeof(result.error_message), "Invalid mermaid header: \"%s\". Expected \"stateDiagram-v2\".", sig.lines[0].content);
        result.error_line = sig.lines[0].line_no;
        return result;
    }

    nixie_mm_graph_t *graph = (nixie_mm_graph_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_mm_graph_t));
    graph->direction        = NIXIE_DIR_TD;
    graph->node_index       = nixie_strmap_create(arena, 64);

    int start_count = 0, end_count = 0;

    for (size_t i = 1; i < sig.count; i++) {
        const char *line = sig.lines[i].content;

        if (ends_with_brace(line) || strcmp(line, "}") == 0) {
            continue; /* composite state start/end: flattened, see parser.h */
        }

        nixie_direction_t dir;
        if (try_direction_line(line, &dir)) {
            graph->direction = dir;
            continue;
        }

        if (starts_with_kw(line, "linkStyle")) {
            continue; /* unsupported in this v1 slice, same as the flowchart parser */
        }

        const char *alias_id;
        size_t alias_id_len;
        char *alias_label;
        if (try_state_alias(arena, line, &alias_id, &alias_id_len, &alias_label)) {
            nixie_mm_find_or_add_node(arena, graph, alias_id, alias_id_len, alias_label, NIXIE_SHAPE_ROUNDED);
            continue;
        }

        if (try_transition(arena, graph, line, &start_count, &end_count)) {
            continue;
        }

        if (try_state_description(arena, graph, line)) {
            continue;
        }

        /* Anything else (unsupported syntax) is silently ignored, matching
         * the flowchart parser's precedent. */
    }

    result.graph = graph;
    return result;
}
