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
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return 1;
}

static size_t skip_ws(const char *s) {
    size_t n = 0;
    while (s[n] == ' ' || s[n] == '\t') {
        n++;
    }
    return n;
}

/* Class diagram syntax always separates ids from arrows/braces with
 * whitespace (mermaid's own class-relationship grammar requires it, unlike
 * the flowchart parser's "-->"-with-no-space shorthand), so a plain run of
 * non-whitespace characters is an unambiguous id scan here -- no hyphen or
 * 'o'-vs-aggregation-arrow lookahead needed. */
static size_t scan_non_ws_len(const char *s) {
    size_t n = 0;
    while (s[n] != '\0' && s[n] != ' ' && s[n] != '\t') {
        n++;
    }
    return n;
}

static int is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int starts_with_kw(const char *line, const char *kw) {
    size_t len = strlen(kw);
    if (strncmp(line, kw, len) != 0) {
        return 0;
    }
    return line[len] == '\0' || line[len] == ' ' || line[len] == '\t';
}

static int ends_with_brace(const char *line) {
    size_t len = strlen(line);
    return len > 0 && line[len - 1] == '{';
}

/* Same simplified subset of normalizeBrTags() every parser in this project
 * uses: strips surrounding quotes, converts <br> variants and literal "\n"
 * to real newlines. */
static char *normalize_label(nixie_arena_t *arena, const char *raw, size_t raw_len) {
    size_t start = 0, end = raw_len;
    if (raw_len >= 2 && raw[0] == '"' && raw[raw_len - 1] == '"') {
        start = 1;
        end = raw_len - 1;
    }

    nixie_strbuf_t sb;
    nixie_strbuf_init(&sb);

    size_t i = start;
    while (i < end) {
        if (raw[i] == '<' && i + 2 < end &&
            (raw[i + 1] == 'b' || raw[i + 1] == 'B') && (raw[i + 2] == 'r' || raw[i + 2] == 'R')) {
            size_t j = i + 3;
            if (j < end && raw[j] == ' ') j++;
            if (j < end && raw[j] == '/') j++;
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
 * Array growth (arena-backed dynamic arrays)
 * ========================================================================== */

static void ensure_class_capacity(nixie_arena_t *arena, nixie_class_diagram_t *d) {
    if (d->class_count < d->class_cap) return;
    size_t new_cap = d->class_cap == 0 ? 8 : d->class_cap * 2;
    nixie_class_node_t *new_classes = (nixie_class_node_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_class_node_t));
    if (d->class_count > 0) memcpy(new_classes, d->classes, d->class_count * sizeof(nixie_class_node_t));
    d->classes = new_classes;
    d->class_cap = new_cap;
}

static void ensure_relationship_capacity(nixie_arena_t *arena, nixie_class_diagram_t *d) {
    if (d->relationship_count < d->relationship_cap) return;
    size_t new_cap = d->relationship_cap == 0 ? 8 : d->relationship_cap * 2;
    nixie_class_relationship_t *new_rels =
        (nixie_class_relationship_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_class_relationship_t));
    if (d->relationship_count > 0) memcpy(new_rels, d->relationships, d->relationship_count * sizeof(nixie_class_relationship_t));
    d->relationships = new_rels;
    d->relationship_cap = new_cap;
}

static void ensure_attribute_capacity(nixie_arena_t *arena, nixie_class_node_t *c) {
    if (c->attribute_count < c->attribute_cap) return;
    size_t new_cap = c->attribute_cap == 0 ? 4 : c->attribute_cap * 2;
    nixie_class_member_t *new_attrs = (nixie_class_member_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_class_member_t));
    if (c->attribute_count > 0) memcpy(new_attrs, c->attributes, c->attribute_count * sizeof(nixie_class_member_t));
    c->attributes = new_attrs;
    c->attribute_cap = new_cap;
}

static void ensure_method_capacity(nixie_arena_t *arena, nixie_class_node_t *c) {
    if (c->method_count < c->method_cap) return;
    size_t new_cap = c->method_cap == 0 ? 4 : c->method_cap * 2;
    nixie_class_member_t *new_methods = (nixie_class_member_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_class_member_t));
    if (c->method_count > 0) memcpy(new_methods, c->methods, c->method_count * sizeof(nixie_class_member_t));
    c->methods = new_methods;
    c->method_cap = new_cap;
}

/* Finds or creates a class by id (label defaults to the id itself). */
static int ensure_class(nixie_arena_t *arena, nixie_class_diagram_t *d, const char *id, size_t id_len) {
    int existing = nixie_strmap_get(d->class_index, id, id_len);
    if (existing >= 0) return existing;

    ensure_class_capacity(arena, d);
    int idx = (int)d->class_count;

    char *id_copy = nixie_arena_strndup(arena, id, id_len);
    memset(&d->classes[idx], 0, sizeof(nixie_class_node_t));
    d->classes[idx].id = id_copy;
    d->classes[idx].label = id_copy;
    d->class_count++;

    nixie_strmap_put(d->class_index, id, id_len, idx);
    return idx;
}

/* ==========================================================================
 * Member parsing: "[+-#~]name(params)? [type]" -- attribute or method.
 * ========================================================================== */

static int parse_member(nixie_arena_t *arena, const char *text, nixie_class_member_t *out) {
    memset(out, 0, sizeof(*out));

    size_t len = strlen(text);
    if (len > 0 && text[len - 1] == ';') len--;

    size_t pos = 0;
    if (len > 0 && (text[0] == '+' || text[0] == '-' || text[0] == '#' || text[0] == '~')) {
        switch (text[0]) {
            case '+': out->visibility = NIXIE_VIS_PUBLIC; break;
            case '-': out->visibility = NIXIE_VIS_PRIVATE; break;
            case '#': out->visibility = NIXIE_VIS_PROTECTED; break;
            default: out->visibility = NIXIE_VIS_PACKAGE; break;
        }
        pos = 1;
        while (pos < len && (text[pos] == ' ' || text[pos] == '\t')) pos++;
    }

    const char *rest = text + pos;
    size_t rest_len = len - pos;
    if (rest_len == 0) return 0;

    const char *rest_end = rest + rest_len;
    const char *paren = memchr(rest, '(', rest_len);
    const char *close = NULL;
    if (paren != NULL) {
        close = memchr(paren + 1, ')', (size_t)(rest_end - (paren + 1)));
    }

    if (paren != NULL && close != NULL) {
        size_t name_len = (size_t)(paren - rest);
        while (name_len > 0 && (rest[name_len - 1] == ' ' || rest[name_len - 1] == '\t')) name_len--;
        char *name = nixie_arena_strndup(arena, rest, name_len);

        size_t params_len = (size_t)(close - (paren + 1));
        char *params = params_len > 0 ? nixie_arena_strndup(arena, paren + 1, params_len) : NULL;

        const char *after = close + 1;
        size_t after_len = (size_t)(rest_end - after);
        while (after_len > 0 && (*after == ' ' || *after == '\t')) {
            after++;
            after_len--;
        }
        char *type = after_len > 0 ? nixie_arena_strndup(arena, after, after_len) : NULL;

        size_t nl = strlen(name);
        int is_static = nl > 0 && name[nl - 1] == '$';
        int is_abstract = nl > 0 && name[nl - 1] == '*';
        if ((is_static || is_abstract) && nl > 0) name[nl - 1] = '\0';

        out->name = name;
        out->type = type;
        out->params = params;
        out->is_static = is_static;
        out->is_abstract = is_abstract;
        out->is_method = 1;
        return 1;
    }

    size_t sp = 0;
    while (sp < rest_len && rest[sp] != ' ' && rest[sp] != '\t') sp++;

    char *name, *type = NULL;
    if (sp < rest_len) {
        type = nixie_arena_strndup(arena, rest, sp);
        size_t name_start = sp;
        while (name_start < rest_len && (rest[name_start] == ' ' || rest[name_start] == '\t')) name_start++;
        name = nixie_arena_strndup(arena, rest + name_start, rest_len - name_start);
    } else {
        name = nixie_arena_strndup(arena, rest, rest_len);
    }

    size_t nl = strlen(name);
    int is_static = nl > 0 && name[nl - 1] == '$';
    int is_abstract = nl > 0 && name[nl - 1] == '*';
    if ((is_static || is_abstract) && nl > 0) name[nl - 1] = '\0';

    out->name = name;
    out->type = type;
    out->is_static = is_static;
    out->is_abstract = is_abstract;
    out->is_method = 0;
    return 1;
}

/* ==========================================================================
 * `class Id`, `class Id { ... }`, `class Id~Generic~ { ... }` header scan
 * ========================================================================== */

static int scan_class_header(
    const char *line, size_t *id_start, size_t *id_len, size_t *generic_start, size_t *generic_len, size_t *pos_out) {
    if (!starts_with_kw(line, "class")) return 0;
    size_t i = strlen("class");
    i += skip_ws(line + i);

    size_t start = i;
    while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' && line[i] != '~' && line[i] != '{') i++;
    size_t len = i - start;
    if (len == 0) return 0;

    *id_start = start;
    *id_len = len;
    *generic_start = 0;
    *generic_len = 0;

    if (line[i] == '~') {
        i++;
        size_t gstart = i;
        while (is_word_char(line[i])) i++;
        size_t glen = i - gstart;
        if (glen == 0 || line[i] != '~') return 0;
        *generic_start = gstart;
        *generic_len = glen;
        i++;
    }

    i += skip_ws(line + i);
    *pos_out = i;
    return 1;
}

/* ==========================================================================
 * Relationship parsing
 * ========================================================================== */

typedef struct {
    const char *op;
    nixie_relationship_type_t type;
    nixie_marker_at_t marker_at;
} rel_op_t;

/* Ordered longest-prefix-first: "--" is a prefix of "--|>"/"--*"/"--o"/"-->"
 * and must be tried last, or it would shadow every longer/more specific
 * operator. */
static const rel_op_t REL_OPS[] = {
    {"<|--", NIXIE_REL_INHERITANCE, NIXIE_MARKER_FROM},
    {"--|>", NIXIE_REL_INHERITANCE, NIXIE_MARKER_TO},
    {"<|..", NIXIE_REL_REALIZATION, NIXIE_MARKER_FROM},
    {"..|>", NIXIE_REL_REALIZATION, NIXIE_MARKER_TO},
    {"*--", NIXIE_REL_COMPOSITION, NIXIE_MARKER_FROM},
    {"--*", NIXIE_REL_COMPOSITION, NIXIE_MARKER_TO},
    {"o--", NIXIE_REL_AGGREGATION, NIXIE_MARKER_FROM},
    {"--o", NIXIE_REL_AGGREGATION, NIXIE_MARKER_TO},
    {"-->", NIXIE_REL_ASSOCIATION, NIXIE_MARKER_TO},
    {"<--", NIXIE_REL_ASSOCIATION, NIXIE_MARKER_FROM},
    {"..>", NIXIE_REL_DEPENDENCY, NIXIE_MARKER_TO},
    {"<..", NIXIE_REL_DEPENDENCY, NIXIE_MARKER_FROM},
    {"--", NIXIE_REL_ASSOCIATION, NIXIE_MARKER_TO},
};
#define REL_OP_COUNT (sizeof(REL_OPS) / sizeof(REL_OPS[0]))

static int try_relationship(nixie_arena_t *arena, nixie_class_diagram_t *d, const char *line) {
    const char *p = line;

    size_t from_len = scan_non_ws_len(p);
    if (from_len == 0) return 0;
    const char *from_ptr = p;
    p += from_len;

    size_t ws = skip_ws(p);
    if (ws == 0) return 0;
    p += ws;

    char *from_card = NULL;
    if (*p == '"') {
        const char *close = strchr(p + 1, '"');
        if (close != NULL) {
            from_card = normalize_label(arena, p + 1, (size_t)(close - (p + 1)));
            p = close + 1;
            ws = skip_ws(p);
            if (ws == 0) return 0;
            p += ws;
        }
    }

    const rel_op_t *matched = NULL;
    for (size_t i = 0; i < REL_OP_COUNT; i++) {
        size_t oplen = strlen(REL_OPS[i].op);
        if (strncmp(p, REL_OPS[i].op, oplen) == 0) {
            matched = &REL_OPS[i];
            p += oplen;
            break;
        }
    }
    if (matched == NULL) return 0;

    ws = skip_ws(p);
    if (ws == 0) return 0;
    p += ws;

    char *to_card = NULL;
    if (*p == '"') {
        const char *close = strchr(p + 1, '"');
        if (close != NULL) {
            to_card = normalize_label(arena, p + 1, (size_t)(close - (p + 1)));
            p = close + 1;
            ws = skip_ws(p);
            if (ws == 0) return 0;
            p += ws;
        }
    }

    size_t to_len = scan_non_ws_len(p);
    if (to_len == 0) return 0;
    const char *to_ptr = p;
    p += to_len;

    p += skip_ws(p);

    char *label = NULL;
    if (*p == ':') {
        p++;
        p += skip_ws(p);
        size_t label_len = strlen(p);
        label = normalize_label(arena, p, label_len);
        p += label_len;
    }

    if (*p != '\0') return 0; /* trailing garbage: not a valid relationship line */

    int from_idx = ensure_class(arena, d, from_ptr, from_len);
    int to_idx = ensure_class(arena, d, to_ptr, to_len);

    ensure_relationship_capacity(arena, d);
    nixie_class_relationship_t *rel = &d->relationships[d->relationship_count++];
    rel->from_idx = from_idx;
    rel->to_idx = to_idx;
    rel->type = matched->type;
    rel->marker_at = matched->marker_at;
    rel->label = label;
    rel->from_cardinality = from_card;
    rel->to_cardinality = to_card;

    return 1;
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

nixie_class_parse_result_t nixie_class_parse(nixie_arena_t *arena, const char *text) {
    nixie_class_parse_result_t result;
    result.diagram = NULL;
    result.error = NIXIE_OK;
    result.error_message[0] = '\0';
    result.error_line = -1;

    nixie_sig_lines_t sig = nixie_split_significant_lines(arena, text);

    if (sig.count == 0) {
        result.error = NIXIE_ERROR_EMPTY_INPUT;
        snprintf(result.error_message, sizeof(result.error_message), "Empty mermaid diagram");
        return result;
    }

    if (!(strlen(sig.lines[0].content) == 12 && ieq_n(sig.lines[0].content, "classDiagram", 12))) {
        result.error = NIXIE_ERROR_UNKNOWN_HEADER;
        snprintf(result.error_message, sizeof(result.error_message),
                 "Invalid mermaid header: \"%s\". Expected \"classDiagram\".", sig.lines[0].content);
        result.error_line = sig.lines[0].line_no;
        return result;
    }

    nixie_class_diagram_t *diagram = (nixie_class_diagram_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_class_diagram_t));
    diagram->class_index = nixie_strmap_create(arena, 64);

    int current_class_idx = -1;
    int class_brace_depth = 0;
    int namespace_active = 0;

    for (size_t li = 1; li < sig.count; li++) {
        const char *line = sig.lines[li].content;

        if (current_class_idx >= 0 && class_brace_depth > 0) {
            if (strcmp(line, "}") == 0) {
                class_brace_depth--;
                if (class_brace_depth == 0) current_class_idx = -1;
                continue;
            }

            size_t len = strlen(line);
            if (len >= 5 && line[0] == '<' && line[1] == '<' && line[len - 2] == '>' && line[len - 1] == '>') {
                size_t wstart = 2;
                size_t wlen = len - 4;
                if (wlen > 0) {
                    diagram->classes[current_class_idx].annotation = nixie_arena_strndup(arena, line + wstart, wlen);
                }
                continue;
            }

            nixie_class_member_t member;
            if (parse_member(arena, line, &member)) {
                nixie_class_node_t *cls = &diagram->classes[current_class_idx];
                if (member.is_method) {
                    ensure_method_capacity(arena, cls);
                    cls->methods[cls->method_count++] = member;
                } else {
                    ensure_attribute_capacity(arena, cls);
                    cls->attributes[cls->attribute_count++] = member;
                }
            }
            continue;
        }

        if (starts_with_kw(line, "namespace") && ends_with_brace(line)) {
            namespace_active = 1;
            continue;
        }
        if (strcmp(line, "}") == 0 && namespace_active) {
            namespace_active = 0;
            continue;
        }

        size_t id_start, id_len, generic_start, generic_len, pos;
        if (scan_class_header(line, &id_start, &id_len, &generic_start, &generic_len, &pos)) {
            if (line[pos] == '{' && line[pos + 1] == '\0') {
                int idx = ensure_class(arena, diagram, line + id_start, id_len);
                if (generic_len > 0) {
                    nixie_strbuf_t sb;
                    nixie_strbuf_init(&sb);
                    nixie_strbuf_append_n(&sb, line + id_start, id_len);
                    nixie_strbuf_append_char(&sb, '<');
                    nixie_strbuf_append_n(&sb, line + generic_start, generic_len);
                    nixie_strbuf_append_char(&sb, '>');
                    diagram->classes[idx].label = nixie_arena_strdup(arena, sb.data);
                    nixie_strbuf_free(&sb);
                }
                current_class_idx = idx;
                class_brace_depth = 1;
                continue;
            }
            if (line[pos] == '\0') {
                int idx = ensure_class(arena, diagram, line + id_start, id_len);
                if (generic_len > 0) {
                    nixie_strbuf_t sb;
                    nixie_strbuf_init(&sb);
                    nixie_strbuf_append_n(&sb, line + id_start, id_len);
                    nixie_strbuf_append_char(&sb, '<');
                    nixie_strbuf_append_n(&sb, line + generic_start, generic_len);
                    nixie_strbuf_append_char(&sb, '>');
                    diagram->classes[idx].label = nixie_arena_strdup(arena, sb.data);
                    nixie_strbuf_free(&sb);
                }
                continue;
            }
            /* `class Id { <<annotation>> }` single-line form */
            if (line[pos] == '{') {
                size_t j = pos + 1;
                j += skip_ws(line + j);
                if (line[j] == '<' && line[j + 1] == '<') {
                    j += 2;
                    size_t astart = j;
                    while (is_word_char(line[j])) j++;
                    size_t alen = j - astart;
                    if (alen > 0 && line[j] == '>' && line[j + 1] == '>') {
                        j += 2;
                        j += skip_ws(line + j);
                        if (line[j] == '}') {
                            j++;
                            j += skip_ws(line + j);
                            if (line[j] == '\0') {
                                int idx = ensure_class(arena, diagram, line + id_start, id_len);
                                diagram->classes[idx].annotation = nixie_arena_strndup(arena, line + astart, alen);
                                continue;
                            }
                        }
                    }
                }
            }
            /* Header matched but trailing content isn't a recognized shape:
             * fall through and let the generic handlers below (inline
             * attribute / relationship) have a try at the whole line. */
        }

        /* `Id : member` inline attribute. Tried before try_relationship()
         * below, but this can never misread a real relationship line:
         * every relationship operator (or its optional quoted cardinality)
         * starts right after the first whitespace run with '<', '"', '*',
         * 'o', '-' or '.' -- never ':' -- so a line shaped like a
         * relationship simply fails this check's `line[j] == ':'` test and
         * falls through untouched. */
        {
            size_t id_len2 = scan_non_ws_len(line);
            if (id_len2 > 0) {
                size_t j = id_len2;
                j += skip_ws(line + j);
                if (line[j] == ':') {
                    j++;
                    j += skip_ws(line + j);
                    if (line[j] != '\0') {
                        nixie_class_member_t member;
                        if (parse_member(arena, line + j, &member)) {
                            int idx = ensure_class(arena, diagram, line, id_len2);
                            nixie_class_node_t *cls = &diagram->classes[idx];
                            if (member.is_method) {
                                ensure_method_capacity(arena, cls);
                                cls->methods[cls->method_count++] = member;
                            } else {
                                ensure_attribute_capacity(arena, cls);
                                cls->attributes[cls->attribute_count++] = member;
                            }
                        }
                        continue;
                    }
                }
            }
        }

        if (try_relationship(arena, diagram, line)) {
            continue;
        }

        /* Anything else (unsupported syntax) is silently ignored. */
    }

    result.diagram = diagram;
    return result;
}
