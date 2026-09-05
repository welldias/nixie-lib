#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../flowchart/graph_build.h" /* nixie_split_significant_lines() is generic (text -> lines) */
#include "../strbuf.h"

/* ==========================================================================
 * Small scanning helpers (same small local copies every parser in this
 * project carries -- see class/parser.c's note on why these aren't shared)
 * ========================================================================== */

/* Unlike class/parser.c's ieq_n() (always called after a strlen(...) == n
 * guard, so it's safe to read n bytes unconditionally), this parser calls
 * ieq_n on arbitrary line positions without such a guard (e.g. probing
 * whether a short line starts with "Note" or "left of"), so it must stop at
 * a's NUL terminator itself rather than risk reading past it. */
static int ieq_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ca = a[i];
        if (ca == '\0' || tolower((unsigned char)ca) != tolower((unsigned char)b[i])) {
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

static size_t rtrim_len(const char *s, size_t len) {
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        len--;
    }
    return len;
}

static size_t scan_non_ws_len(const char *s) {
    size_t n = 0;
    while (s[n] != '\0' && s[n] != ' ' && s[n] != '\t') {
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

static void ensure_actor_capacity(nixie_arena_t *arena, nixie_seq_diagram_t *d) {
    if (d->actor_count < d->actor_cap) return;
    size_t new_cap = d->actor_cap == 0 ? 8 : d->actor_cap * 2;
    nixie_seq_actor_t *new_actors = (nixie_seq_actor_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_seq_actor_t));
    if (d->actor_count > 0) memcpy(new_actors, d->actors, d->actor_count * sizeof(nixie_seq_actor_t));
    d->actors = new_actors;
    d->actor_cap = new_cap;
}

static void ensure_message_capacity(nixie_arena_t *arena, nixie_seq_diagram_t *d) {
    if (d->message_count < d->message_cap) return;
    size_t new_cap = d->message_cap == 0 ? 8 : d->message_cap * 2;
    nixie_seq_message_t *new_msgs = (nixie_seq_message_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_seq_message_t));
    if (d->message_count > 0) memcpy(new_msgs, d->messages, d->message_count * sizeof(nixie_seq_message_t));
    d->messages = new_msgs;
    d->message_cap = new_cap;
}

static void ensure_block_capacity(nixie_arena_t *arena, nixie_seq_diagram_t *d) {
    if (d->block_count < d->block_cap) return;
    size_t new_cap = d->block_cap == 0 ? 4 : d->block_cap * 2;
    nixie_seq_block_t *new_blocks = (nixie_seq_block_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_seq_block_t));
    if (d->block_count > 0) memcpy(new_blocks, d->blocks, d->block_count * sizeof(nixie_seq_block_t));
    d->blocks = new_blocks;
    d->block_cap = new_cap;
}

static void ensure_note_capacity(nixie_arena_t *arena, nixie_seq_diagram_t *d) {
    if (d->note_count < d->note_cap) return;
    size_t new_cap = d->note_cap == 0 ? 4 : d->note_cap * 2;
    nixie_seq_note_t *new_notes = (nixie_seq_note_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_seq_note_t));
    if (d->note_count > 0) memcpy(new_notes, d->notes, d->note_count * sizeof(nixie_seq_note_t));
    d->notes = new_notes;
    d->note_cap = new_cap;
}

static void ensure_note_actor_capacity(nixie_arena_t *arena, nixie_seq_note_t *n) {
    if (n->actor_count < n->actor_cap) return;
    size_t new_cap = n->actor_cap == 0 ? 4 : n->actor_cap * 2;
    int *new_idx = (int *)nixie_arena_alloc(arena, new_cap * sizeof(int));
    if (n->actor_count > 0) memcpy(new_idx, n->actor_indices, n->actor_count * sizeof(int));
    n->actor_indices = new_idx;
    n->actor_cap = new_cap;
}

/* Block-nesting stack used only while parsing (discarded once each block is
 * closed and folded into diagram->blocks), mirroring parser.ts's local
 * `blockStack` array. Arena-backed so pathologically deep nesting doesn't
 * overflow a fixed-size C stack array. */
typedef struct block_stack_entry {
    nixie_seq_block_type_t type;
    char *label;
    int start_index;
    nixie_seq_divider_t *dividers;
    size_t divider_count;
    size_t divider_cap;
} block_stack_entry_t;

typedef struct block_stack {
    block_stack_entry_t *entries;
    size_t count;
    size_t cap;
} block_stack_t;

static void ensure_stack_entry_divider_capacity(nixie_arena_t *arena, block_stack_entry_t *e) {
    if (e->divider_count < e->divider_cap) return;
    size_t new_cap = e->divider_cap == 0 ? 4 : e->divider_cap * 2;
    nixie_seq_divider_t *new_divs = (nixie_seq_divider_t *)nixie_arena_alloc(arena, new_cap * sizeof(nixie_seq_divider_t));
    if (e->divider_count > 0) memcpy(new_divs, e->dividers, e->divider_count * sizeof(nixie_seq_divider_t));
    e->dividers = new_divs;
    e->divider_cap = new_cap;
}

static void stack_push(nixie_arena_t *arena, block_stack_t *s, block_stack_entry_t e) {
    if (s->count == s->cap) {
        size_t new_cap = s->cap == 0 ? 8 : s->cap * 2;
        block_stack_entry_t *new_entries = (block_stack_entry_t *)nixie_arena_alloc(arena, new_cap * sizeof(block_stack_entry_t));
        if (s->count > 0) memcpy(new_entries, s->entries, s->count * sizeof(block_stack_entry_t));
        s->entries = new_entries;
        s->cap = new_cap;
    }
    s->entries[s->count++] = e;
}

/* ==========================================================================
 * Actor lookup / auto-creation
 * ========================================================================== */

/* Registers an actor the first time its id is seen; subsequent calls with
 * the same id return the existing index UNCHANGED, even if a later explicit
 * "participant X as Y" declaration follows a message that already
 * referenced X -- this mirrors parser.ts's ensureActor()/actorMatch, which
 * both gate on the same `actorIds` set and simply no-op if already present
 * (i.e. "first reference wins", the same quirky precedent already recorded
 * for state-diagram descriptions). `label` NULL means "default to the id". */
static int ensure_actor(
    nixie_arena_t *arena, nixie_seq_diagram_t *d, const char *id, size_t id_len,
    const char *label, size_t label_len, nixie_seq_actor_type_t type) {
    int existing = nixie_strmap_get(d->actor_index, id, id_len);
    if (existing >= 0) return existing;

    ensure_actor_capacity(arena, d);
    int idx = (int)d->actor_count;
    nixie_seq_actor_t *a = &d->actors[d->actor_count++];
    a->id = nixie_arena_strndup(arena, id, id_len);
    a->label = label != NULL ? normalize_label(arena, label, label_len) : nixie_arena_strndup(arena, id, id_len);
    a->type = type;
    nixie_strmap_put(d->actor_index, a->id, id_len, idx);
    return idx;
}

/* ==========================================================================
 * Message arrow operators
 *
 * Ported from parser.ts's msgMatch/simpleMsgMatch regex alternatives, which
 * together cover exactly these 8 operators (the two regexes overlap almost
 * entirely; "--?>>" / "--?>" in msgMatch's alternation are dead branches
 * already subsumed by "--?>?>", so they aren't duplicated here). Ordered
 * longest-first: "-->" is a textual prefix of "-->>", and "->" of "->>", so
 * a shorter entry must never be checked before its longer superstring or it
 * would consume only part of the operator and misparse the remainder.
 * ========================================================================== */

typedef struct msg_arrow_op {
    const char *op;
    int dashed;
    int filled;
} msg_arrow_op_t;

static const msg_arrow_op_t MSG_ARROW_OPS[] = {
    {"-->>", 1, 1},
    {"->>", 0, 1},
    {"--x", 1, 1},
    {"--)", 1, 0},
    {"-->", 1, 0},
    {"-x", 0, 1},
    {"-)", 0, 0},
    {"->", 0, 0},
};
#define MSG_ARROW_OP_COUNT (sizeof(MSG_ARROW_OPS) / sizeof(MSG_ARROW_OPS[0]))

/* Scans an actor-id token, stopping before a hyphen that starts one of the
 * arrow operators above, so a hyphenated id directly abutting an arrow with
 * no separating space (e.g. "web-server->>B: hi", a common real-world
 * mermaid style) isn't eaten into by the arrow -- the same lookahead trick
 * flowchart/parser.c uses for its own "A-->B" no-space case. */
static size_t scan_actor_id_len(const char *s) {
    size_t n = 0;
    while (s[n] != '\0' && s[n] != ' ' && s[n] != '\t') {
        if (s[n] == '-') {
            int is_arrow_start = 0;
            for (size_t k = 0; k < MSG_ARROW_OP_COUNT; k++) {
                size_t oplen = strlen(MSG_ARROW_OPS[k].op);
                if (strncmp(s + n, MSG_ARROW_OPS[k].op, oplen) == 0) {
                    is_arrow_start = 1;
                    break;
                }
            }
            if (is_arrow_start) break;
        }
        n++;
    }
    return n;
}

/* ==========================================================================
 * Per-line try_* parsers
 * ========================================================================== */

static int try_actor_decl(nixie_arena_t *arena, nixie_seq_diagram_t *d, const char *line) {
    nixie_seq_actor_type_t type;
    size_t kwlen;
    if (starts_with_kw(line, "participant")) {
        type = NIXIE_SEQ_PARTICIPANT;
        kwlen = strlen("participant");
    } else if (starts_with_kw(line, "actor")) {
        type = NIXIE_SEQ_ACTOR;
        kwlen = strlen("actor");
    } else {
        return 0;
    }

    size_t i = kwlen;
    size_t ws = skip_ws(line + i);
    if (ws == 0) return 0;
    i += ws;

    size_t id_len = scan_non_ws_len(line + i);
    if (id_len == 0) return 0;
    const char *id_ptr = line + i;
    i += id_len;

    size_t j = i + skip_ws(line + i);

    const char *label_ptr = NULL;
    size_t label_len = 0;
    if (strncmp(line + j, "as", 2) == 0 && (line[j + 2] == ' ' || line[j + 2] == '\t')) {
        j += 2;
        j += skip_ws(line + j);
        size_t raw_len = strlen(line + j);
        if (raw_len == 0) return 0;
        label_ptr = line + j;
        label_len = rtrim_len(label_ptr, raw_len);
    } else if (line[i] != '\0') {
        return 0; /* trailing content that isn't " as ..." */
    }

    ensure_actor(arena, d, id_ptr, id_len, label_ptr, label_len, type);
    return 1;
}

static int try_note(nixie_arena_t *arena, nixie_seq_diagram_t *d, const char *line) {
    if (!ieq_n(line, "Note", 4) || !(line[4] == ' ' || line[4] == '\t')) return 0;
    size_t i = 4 + skip_ws(line + 4);

    nixie_seq_note_position_t pos;
    size_t kwlen;
    if (ieq_n(line + i, "left of", 7) && (line[i + 7] == ' ' || line[i + 7] == '\t')) {
        pos = NIXIE_SEQ_NOTE_LEFT;
        kwlen = 7;
    } else if (ieq_n(line + i, "right of", 8) && (line[i + 8] == ' ' || line[i + 8] == '\t')) {
        pos = NIXIE_SEQ_NOTE_RIGHT;
        kwlen = 8;
    } else if (ieq_n(line + i, "over", 4) && (line[i + 4] == ' ' || line[i + 4] == '\t')) {
        pos = NIXIE_SEQ_NOTE_OVER;
        kwlen = 4;
    } else {
        return 0;
    }
    i += kwlen;
    size_t ws = skip_ws(line + i);
    if (ws == 0) return 0;
    i += ws;

    const char *colon = strchr(line + i, ':');
    if (colon == NULL) return 0;

    size_t actors_start = i;
    size_t actors_len = rtrim_len(line + actors_start, (size_t)(colon - (line + actors_start)));
    if (actors_len == 0) return 0;

    size_t j = (size_t)(colon - line) + 1;
    j += skip_ws(line + j);
    size_t text_raw_len = strlen(line + j);
    if (text_raw_len == 0) return 0;

    ensure_note_capacity(arena, d);
    nixie_seq_note_t *note = &d->notes[d->note_count++];
    memset(note, 0, sizeof(*note));
    note->position = pos;
    note->text = normalize_label(arena, line + j, text_raw_len);
    note->after_index = (int)d->message_count - 1;

    /* Split the comma-separated actor list, auto-creating any actor not
     * already declared, exactly like every other reference in this parser. */
    const char *p = line + actors_start;
    const char *seg_end = line + actors_start + actors_len;
    while (p < seg_end) {
        while (p < seg_end && (*p == ' ' || *p == '\t')) p++;
        const char *seg_start = p;
        while (p < seg_end && *p != ',') p++;
        size_t seg_len = rtrim_len(seg_start, (size_t)(p - seg_start));
        if (seg_len > 0) {
            int idx = ensure_actor(arena, d, seg_start, seg_len, NULL, 0, NIXIE_SEQ_PARTICIPANT);
            ensure_note_actor_capacity(arena, note);
            note->actor_indices[note->actor_count++] = idx;
        }
        if (p < seg_end) p++; /* skip ',' */
    }

    return note->actor_count > 0;
}

static int try_block_start(nixie_arena_t *arena, block_stack_t *stack, nixie_seq_diagram_t *d, const char *line) {
    static const struct {
        const char *kw;
        nixie_seq_block_type_t type;
    } kws[] = {
        {"loop", NIXIE_SEQ_BLOCK_LOOP},
        {"alt", NIXIE_SEQ_BLOCK_ALT},
        {"opt", NIXIE_SEQ_BLOCK_OPT},
        {"par", NIXIE_SEQ_BLOCK_PAR},
        {"critical", NIXIE_SEQ_BLOCK_CRITICAL},
        {"break", NIXIE_SEQ_BLOCK_BREAK},
        {"rect", NIXIE_SEQ_BLOCK_RECT},
    };

    for (size_t k = 0; k < sizeof(kws) / sizeof(kws[0]); k++) {
        size_t kwlen = strlen(kws[k].kw);
        if (strncmp(line, kws[k].kw, kwlen) != 0) continue;
        if (line[kwlen] != '\0' && line[kwlen] != ' ' && line[kwlen] != '\t') continue;

        size_t i = kwlen + skip_ws(line + kwlen);
        size_t raw_len = strlen(line + i);
        size_t label_len = rtrim_len(line + i, raw_len);

        block_stack_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        entry.type = kws[k].type;
        entry.label = normalize_label(arena, line + i, label_len);
        entry.start_index = (int)d->message_count;
        stack_push(arena, stack, entry);
        return 1;
    }
    return 0;
}

static int try_divider(nixie_arena_t *arena, block_stack_t *stack, nixie_seq_diagram_t *d, const char *line) {
    if (stack->count == 0) return 0;

    size_t kwlen;
    if (starts_with_kw(line, "else")) {
        kwlen = strlen("else");
    } else if (starts_with_kw(line, "and")) {
        kwlen = strlen("and");
    } else {
        return 0;
    }

    size_t i = kwlen + skip_ws(line + kwlen);
    size_t raw_len = strlen(line + i);
    size_t label_len = rtrim_len(line + i, raw_len);

    block_stack_entry_t *top = &stack->entries[stack->count - 1];
    ensure_stack_entry_divider_capacity(arena, top);
    nixie_seq_divider_t *div = &top->dividers[top->divider_count++];
    div->index = (int)d->message_count;
    div->label = normalize_label(arena, line + i, label_len);
    return 1;
}

static int try_message(nixie_arena_t *arena, nixie_seq_diagram_t *d, const char *line) {
    size_t from_len = scan_actor_id_len(line);
    if (from_len == 0) return 0;
    const char *from_ptr = line;
    size_t i = from_len;
    i += skip_ws(line + i);

    const msg_arrow_op_t *matched = NULL;
    for (size_t k = 0; k < MSG_ARROW_OP_COUNT; k++) {
        size_t oplen = strlen(MSG_ARROW_OPS[k].op);
        if (strncmp(line + i, MSG_ARROW_OPS[k].op, oplen) == 0) {
            matched = &MSG_ARROW_OPS[k];
            i += oplen;
            break;
        }
    }
    if (matched == NULL) return 0;

    int activate = 0, deactivate = 0;
    if (line[i] == '+') {
        activate = 1;
        i++;
    } else if (line[i] == '-') {
        deactivate = 1;
        i++;
    }

    size_t to_len = 0;
    while (line[i + to_len] != '\0' && line[i + to_len] != ' ' && line[i + to_len] != '\t' && line[i + to_len] != ':') {
        to_len++;
    }
    if (to_len == 0) return 0;
    const char *to_ptr = line + i;
    i += to_len;

    i += skip_ws(line + i);
    if (line[i] != ':') return 0;
    i++;
    i += skip_ws(line + i);
    size_t raw_len = strlen(line + i);
    if (raw_len == 0) return 0;
    size_t label_len = rtrim_len(line + i, raw_len);
    if (label_len == 0) return 0;

    int from_idx = ensure_actor(arena, d, from_ptr, from_len, NULL, 0, NIXIE_SEQ_PARTICIPANT);
    int to_idx = ensure_actor(arena, d, to_ptr, to_len, NULL, 0, NIXIE_SEQ_PARTICIPANT);

    ensure_message_capacity(arena, d);
    nixie_seq_message_t *m = &d->messages[d->message_count++];
    m->from_idx = from_idx;
    m->to_idx = to_idx;
    m->label = normalize_label(arena, line + i, label_len);
    m->line_style = matched->dashed ? NIXIE_SEQ_DASHED : NIXIE_SEQ_SOLID;
    m->arrow_head = matched->filled ? NIXIE_SEQ_FILLED : NIXIE_SEQ_OPEN;
    m->activate = activate;
    m->deactivate = deactivate;
    return 1;
}

/* ==========================================================================
 * Entry point
 * ========================================================================== */

nixie_sequence_parse_result_t nixie_sequence_parse(nixie_arena_t *arena, const char *text) {
    nixie_sequence_parse_result_t result;
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

    /* Unlike class/parser.c's exact-match header check, this accepts any
     * trailing content after a whitespace boundary (e.g. the real Mermaid
     * "sequenceDiagram autonumber" directive) -- beautiful-mermaid's own
     * parser.ts never validates the header line at all, it unconditionally
     * skips line 0, so rejecting a legitimate trailing directive here would
     * be a fidelity regression relative to the reference, not a match to
     * it. */
    if (!starts_with_kw(sig.lines[0].content, "sequenceDiagram")) {
        result.error = NIXIE_ERROR_UNKNOWN_HEADER;
        snprintf(result.error_message, sizeof(result.error_message),
                 "Invalid mermaid header: \"%s\". Expected \"sequenceDiagram\".", sig.lines[0].content);
        result.error_line = sig.lines[0].line_no;
        return result;
    }

    nixie_seq_diagram_t *diagram = (nixie_seq_diagram_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_seq_diagram_t));
    diagram->actor_index = nixie_strmap_create(arena, 64);

    block_stack_t stack;
    memset(&stack, 0, sizeof(stack));

    for (size_t li = 1; li < sig.count; li++) {
        const char *line = sig.lines[li].content;

        if (try_actor_decl(arena, diagram, line)) continue;
        if (try_note(arena, diagram, line)) continue;
        if (try_block_start(arena, &stack, diagram, line)) continue;
        if (try_divider(arena, &stack, diagram, line)) continue;

        if (strcmp(line, "end") == 0 && stack.count > 0) {
            block_stack_entry_t completed = stack.entries[--stack.count];
            ensure_block_capacity(arena, diagram);
            nixie_seq_block_t *b = &diagram->blocks[diagram->block_count++];
            b->type = completed.type;
            b->label = completed.label;
            b->start_index = completed.start_index;
            int end_index = (int)diagram->message_count - 1;
            b->end_index = end_index > completed.start_index ? end_index : completed.start_index;
            b->dividers = completed.dividers;
            b->divider_count = completed.divider_count;
            b->divider_cap = completed.divider_cap;
            continue;
        }

        if (try_message(arena, diagram, line)) continue;

        /* Anything else (explicit activate/deactivate, unsupported syntax)
         * is silently ignored, mirroring parser.ts's own final comment. */
    }

    result.diagram = diagram;
    return result;
}
