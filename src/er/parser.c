#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../flowchart/graph_build.h" /* nixie_split_significant_lines() is generic (text -> lines) */
#include "../strbuf.h"

/* ==========================================================================
 * Small scanning helpers
 * ========================================================================== */

static int ieq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    }
    return 1;
}

static size_t skip_ws(const char *s) {
    size_t n = 0;
    while (s[n] == ' ' || s[n] == '\t')
        n++;
    return n;
}

/* Same rationale as class/parser.c's scan_non_ws_law: mermaid's er-diagram
 * grammar always separates ids from cardinality/braces with whitespace, so
 * a plain non-whitespace run is an unambiguous id scan here. */
static size_t scan_non_ws_len(const char *s) {
    size_t n = 0;
    while (s[n] != '\0' && s[n] != ' ' && s[n] != '\t')
        n++;
    return n;
}

static int is_card_char(char c) {
    return c == '|' || c == 'o' || c == '}' || c == '{';
}

/* Strips surrounding double quotes, converts <br>/<br/>/<br /> (any case)
 * and literal "\n" escapes to real newlines -- same simplified subset every
 * parser in this project uses. */
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
 * Array growth
 * ========================================================================== */

static void ensure_entity_capacity(nixie_arena_t *arena, nixie_er_diagram_t *d) {
    if (d->entity_count < d->entity_cap)
        return;
    size_t new_cap                  = d->entity_cap == 0 ? 8 : d->entity_cap * 2;
    nixie_er_entity_t *new_entities = (nixie_er_entity_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_er_entity_t));
    if (d->entity_count > 0)
        memcpy(new_entities, d->entities, d->entity_count * sizeof(nixie_er_entity_t));
    d->entities   = new_entities;
    d->entity_cap = new_cap;
}

static void ensure_relationship_capacity(nixie_arena_t *arena, nixie_er_diagram_t *d) {
    if (d->relationship_count < d->relationship_cap)
        return;
    size_t new_cap                    = d->relationship_cap == 0 ? 8 : d->relationship_cap * 2;
    nixie_er_relationship_t *new_rels = (nixie_er_relationship_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_er_relationship_t));
    if (d->relationship_count > 0)
        memcpy(new_rels, d->relationships, d->relationship_count * sizeof(nixie_er_relationship_t));
    d->relationships    = new_rels;
    d->relationship_cap = new_cap;
}

static void ensure_attribute_capacity(nixie_arena_t *arena, nixie_er_entity_t *e) {
    if (e->attribute_count < e->attribute_cap)
        return;
    size_t new_cap                  = e->attribute_cap == 0 ? 4 : e->attribute_cap * 2;
    nixie_er_attribute_t *new_attrs = (nixie_er_attribute_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_er_attribute_t));
    if (e->attribute_count > 0)
        memcpy(new_attrs, e->attributes, e->attribute_count * sizeof(nixie_er_attribute_t));
    e->attributes    = new_attrs;
    e->attribute_cap = new_cap;
}

static int ensure_entity(nixie_arena_t *arena, nixie_er_diagram_t *d, const char *id, size_t id_len) {
    int existing = nixie_strmap_get(d->entity_index, id, id_len);
    if (existing >= 0)
        return existing;

    ensure_entity_capacity(arena, d);
    int idx = (int)d->entity_count;

    char *id_copy = nixie_arena_strndup(arena, id, id_len);
    memset(&d->entities[idx], 0, sizeof(nixie_er_entity_t));
    d->entities[idx].id    = id_copy;
    d->entities[idx].label = id_copy;
    d->entity_count++;

    nixie_strmap_put(d->entity_index, id, id_len, idx);
    return idx;
}

/* ==========================================================================
 * Attribute parsing: "type name [PK|FK|UK ...] [\"comment\"]"
 * ========================================================================== */

static int try_attribute(nixie_arena_t *arena, const char *line, nixie_er_attribute_t *out) {
    size_t type_len = scan_non_ws_len(line);
    if (type_len == 0)
        return 0;
    size_t i  = type_len;
    size_t ws = skip_ws(line + i);
    if (ws == 0)
        return 0;
    i += ws;

    size_t name_len = scan_non_ws_len(line + i);
    if (name_len == 0)
        return 0;
    const char *name_ptr = line + i;
    i += name_len;

    memset(out, 0, sizeof(*out));
    out->type = nixie_arena_strndup(arena, line, type_len);
    out->name = nixie_arena_strndup(arena, name_ptr, name_len);

    i += skip_ws(line + i);
    const char *rest     = line + i;
    size_t rest_len      = strlen(rest);
    const char *scan_end = rest + rest_len;

    /* Extract the first quoted comment (if any), mirroring
     * beautiful-mermaid/src/er/parser.ts's parseAttribute(). */
    const char *qopen = memchr(rest, '"', rest_len);
    if (qopen != NULL) {
        const char *qclose = memchr(qopen + 1, '"', (size_t)(scan_end - (qopen + 1)));
        if (qclose != NULL) {
            out->comment = normalize_label(arena, qopen + 1, (size_t)(qclose - (qopen + 1)));
        }
    }

    /* Scan remaining whitespace-separated tokens for PK/FK/UK, skipping over
     * any quoted comment span entirely. */
    const char *p = rest;
    while (p < scan_end) {
        while (p < scan_end && (*p == ' ' || *p == '\t'))
            p++;
        if (p >= scan_end)
            break;

        if (*p == '"') {
            const char *close = memchr(p + 1, '"', (size_t)(scan_end - (p + 1)));
            p                 = close != NULL ? close + 1 : scan_end;
            continue;
        }

        const char *tok_start = p;
        while (p < scan_end && *p != ' ' && *p != '\t')
            p++;
        size_t tok_len = (size_t)(p - tok_start);
        if (tok_len == 2) {
            char c0 = (char)toupper((unsigned char)tok_start[0]);
            char c1 = (char)toupper((unsigned char)tok_start[1]);
            if (c0 == 'P' && c1 == 'K')
                out->keys |= NIXIE_ER_KEY_PK;
            else if (c0 == 'F' && c1 == 'K')
                out->keys |= NIXIE_ER_KEY_FK;
            else if (c0 == 'U' && c1 == 'K')
                out->keys |= NIXIE_ER_KEY_UK;
        }
    }

    return 1;
}

/* ==========================================================================
 * Entity block start: `ENTITY_NAME {`
 * ========================================================================== */

static int try_entity_block_start(const char *line, const char **id_ptr, size_t *id_len) {
    size_t len = scan_non_ws_len(line);
    if (len == 0)
        return 0;
    size_t i = len;
    i += skip_ws(line + i);
    if (line[i] != '{' || line[i + 1] != '\0')
        return 0;
    *id_ptr = line;
    *id_len = len;
    return 1;
}

/* ==========================================================================
 * Cardinality: sort the 2-char symbol so both orderings ("o|" and "|o", for
 * example) normalize to one lookup -- the same trick
 * beautiful-mermaid/src/er/parser.ts's parseCardinality() uses.
 *
 * One deviation from that source: it only recognizes the zero-or-many
 * bracket pair as "{o"/"o{" (sorted), which misses the equally valid and
 * arguably more common "}o" spelling of the same cardinality (mermaid's own
 * docs list both "}o" and "o{" as zero-or-many) -- "}o" sorts to "o}", a
 * pattern beautiful-mermaid's own lookup never checks, so it silently drops
 * any relationship written that way. That looks like an unintentional gap
 * rather than a deliberate restriction, so this port adds the "o}" case
 * rather than reproducing the drop.
 * ========================================================================== */

static int parse_cardinality(const char *s, size_t len, nixie_er_cardinality_t *out) {
    if (len != 2)
        return 0;
    char a = s[0], b = s[1];
    char lo = a < b ? a : b, hi = a < b ? b : a;

    if (lo == '|' && hi == '|') {
        *out = NIXIE_ER_ONE;
        return 1;
    }
    if (lo == 'o' && hi == '|') {
        *out = NIXIE_ER_ZERO_ONE;
        return 1;
    }
    if ((lo == '|' && hi == '}') || (lo == '{' && hi == '|')) {
        *out = NIXIE_ER_MANY;
        return 1;
    }
    if ((lo == 'o' && hi == '{') || (lo == '{' && hi == 'o') || (lo == 'o' && hi == '}')) {
        *out = NIXIE_ER_ZERO_MANY;
        return 1;
    }
    return 0;
}

/* ==========================================================================
 * Relationship: `ENTITY1 <cardinality1><--|..><cardinality2> ENTITY2 : label`
 * ========================================================================== */

static int try_relationship(nixie_arena_t *arena, nixie_er_diagram_t *d, const char *line) {
    size_t e1_len = scan_non_ws_len(line);
    if (e1_len == 0)
        return 0;
    const char *e1_ptr = line;
    size_t i           = e1_len;

    size_t ws = skip_ws(line + i);
    if (ws == 0)
        return 0;
    i += ws;

    size_t left_start = i;
    while (is_card_char(line[i]))
        i++;
    size_t left_len = i - left_start;
    if (left_len == 0)
        return 0;

    int identifying;
    if (strncmp(line + i, "--", 2) == 0) {
        identifying = 1;
        i += 2;
    } else if (strncmp(line + i, "..", 2) == 0) {
        identifying = 0;
        i += 2;
    } else {
        return 0;
    }

    size_t right_start = i;
    while (is_card_char(line[i]))
        i++;
    size_t right_len = i - right_start;
    if (right_len == 0)
        return 0;

    ws = skip_ws(line + i);
    if (ws == 0)
        return 0;
    i += ws;

    size_t e2_len = scan_non_ws_len(line + i);
    if (e2_len == 0)
        return 0;
    const char *e2_ptr = line + i;
    i += e2_len;

    i += skip_ws(line + i);
    if (line[i] != ':')
        return 0;
    i++;
    i += skip_ws(line + i);

    size_t label_len = strlen(line + i);
    if (label_len == 0)
        return 0;
    const char *label_start = line + i;

    /* Strip a leading and/or trailing quote (either " or '), independently
     * -- mirrors the JS `.replace(/^["']|["']$/g, '')` on the trimmed label. */
    size_t lstart = 0, lend = label_len;
    if (lend > lstart && (label_start[lstart] == '"' || label_start[lstart] == '\''))
        lstart++;
    if (lend > lstart && (label_start[lend - 1] == '"' || label_start[lend - 1] == '\''))
        lend--;

    nixie_er_cardinality_t c1, c2;
    if (!parse_cardinality(line + left_start, left_len, &c1))
        return 0;
    if (!parse_cardinality(line + right_start, right_len, &c2))
        return 0;

    int e1_idx = ensure_entity(arena, d, e1_ptr, e1_len);
    int e2_idx = ensure_entity(arena, d, e2_ptr, e2_len);

    ensure_relationship_capacity(arena, d);
    nixie_er_relationship_t *rel = &d->relationships[d->relationship_count++];
    rel->entity1_idx             = e1_idx;
    rel->entity2_idx             = e2_idx;
    rel->cardinality1            = c1;
    rel->cardinality2            = c2;
    rel->label                   = normalize_label(arena, label_start + lstart, lend - lstart);
    rel->identifying             = identifying;

    return 1;
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

nixie_er_parse_result_t nixie_er_parse(nixie_arena_t *arena, const char *text) {
    nixie_er_parse_result_t result;
    result.diagram          = NULL;
    result.error            = NIXIE_OK;
    result.error_message[0] = '\0';
    result.error_line       = -1;

    nixie_sig_lines_t sig = nixie_split_significant_lines(arena, text);

    if (sig.count == 0) {
        result.error = NIXIE_ERROR_EMPTY_INPUT;
        snprintf(result.error_message, sizeof(result.error_message), "Empty mermaid diagram");
        return result;
    }

    if (!(strlen(sig.lines[0].content) == 9 && ieq_n(sig.lines[0].content, "erDiagram", 9))) {
        result.error = NIXIE_ERROR_UNKNOWN_HEADER;
        snprintf(result.error_message, sizeof(result.error_message), "Invalid mermaid header: \"%s\". Expected \"erDiagram\".", sig.lines[0].content);
        result.error_line = sig.lines[0].line_no;
        return result;
    }

    nixie_er_diagram_t *diagram = (nixie_er_diagram_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_er_diagram_t));
    diagram->entity_index       = nixie_strmap_create(arena, 64);

    int current_entity_idx = -1;

    for (size_t li = 1; li < sig.count; li++) {
        const char *line = sig.lines[li].content;

        if (current_entity_idx >= 0) {
            if (strcmp(line, "}") == 0) {
                current_entity_idx = -1;
                continue;
            }
            nixie_er_attribute_t attr;
            if (try_attribute(arena, line, &attr)) {
                nixie_er_entity_t *e = &diagram->entities[current_entity_idx];
                ensure_attribute_capacity(arena, e);
                e->attributes[e->attribute_count++] = attr;
            }
            continue;
        }

        const char *id_ptr;
        size_t id_len;
        if (try_entity_block_start(line, &id_ptr, &id_len)) {
            current_entity_idx = ensure_entity(arena, diagram, id_ptr, id_len);
            continue;
        }

        if (try_relationship(arena, diagram, line)) {
            continue;
        }

        /* Anything else (unsupported syntax) is silently ignored. */
    }

    result.diagram = diagram;
    return result;
}
