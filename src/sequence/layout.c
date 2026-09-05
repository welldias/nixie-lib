#include "layout.h"

#include <string.h>

#include "../strbuf.h"
#include "../text_metrics.h"

/* ==========================================================================
 * Layout constants, ported verbatim from
 * beautiful-mermaid/src/sequence/layout.ts's SEQ object.
 * ========================================================================== */

#define SEQ_PADDING 30.0
#define SEQ_ACTOR_GAP 140.0
#define SEQ_ACTOR_HEIGHT 40.0
#define SEQ_ACTOR_PAD_X 16.0
#define SEQ_HEADER_GAP 20.0
#define SEQ_MESSAGE_ROW_HEIGHT 40.0
#define SEQ_SELF_MESSAGE_HEIGHT 30.0
#define SEQ_ACTIVATION_WIDTH 10.0
#define SEQ_BLOCK_PAD_X 10.0
#define SEQ_BLOCK_PAD_TOP 40.0
#define SEQ_BLOCK_PAD_BOTTOM 8.0
#define SEQ_BLOCK_HEADER_EXTRA 28.0
#define SEQ_DIVIDER_EXTRA 24.0
#define SEQ_NOTE_WIDTH 60.0
#define SEQ_NOTE_PAD_X 12.0
#define SEQ_NOTE_PAD_Y 6.0
#define SEQ_NOTE_GAP 10.0
#define SEQ_NESTING_OFFSET 4.0

#define FONT_SIZE_NODE_LABEL 13.0
#define FONT_WEIGHT_NODE_LABEL 500
#define FONT_SIZE_EDGE_LABEL 11.0
#define FONT_WEIGHT_EDGE_LABEL 400

static double dmax(double a, double b) { return a > b ? a : b; }
static double dmin(double a, double b) { return a < b ? a : b; }

/* Per-actor activation stack, tracked only during layout (mirrors
 * layout.ts's `activationStacks` Map<actorId, {startY,depth}[]>). */
typedef struct act_stack {
    double *start_y;
    int *depth;
    size_t count;
    size_t cap;
} act_stack_t;

/* Positions every note whose after_index matches `after_index`, stacking
 * them downward from `*note_y_inout` (which the caller seeds with the
 * anchor y for this point in the timeline, and which this function advances
 * past the notes it placed). Returns 1 if any note was placed.
 *
 * Extracted out of the per-message loop below so it can also be called once
 * *before* that loop for after_index == -1 (a note written before the first
 * message) -- see the note above nixie_sequence_layout() on why that case
 * needs handling here even though layout.ts's own reference implementation
 * never positions such notes at all (a real gap in the JS: its
 * notesByAfterIndex map is only ever queried for msgIdx >= 0 inside the
 * per-message loop, so a note keyed -1 is silently dropped). */
static int position_notes_for(
    nixie_arena_t *arena, const nixie_seq_diagram_t *diagram, const double *actor_center_x, const double *actor_widths,
    int after_index, double *note_y_inout, nixie_ps_note_t *notes, size_t *note_out_count) {
    int any = 0;
    double note_y = *note_y_inout;

    for (size_t ni = 0; ni < diagram->note_count; ni++) {
        const nixie_seq_note_t *note = &diagram->notes[ni];
        if (note->after_index != after_index) continue;
        any = 1;

        double note_w = dmax(
            SEQ_NOTE_WIDTH, nixie_measure_text_width(note->text, FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL) + SEQ_NOTE_PAD_X * 2.0);
        double note_h = FONT_SIZE_EDGE_LABEL + SEQ_NOTE_PAD_Y * 2.0;

        size_t first_actor_idx = note->actor_count > 0 ? (size_t)note->actor_indices[0] : 0;
        double note_x;
        if (note->position == NIXIE_SEQ_NOTE_LEFT) {
            note_x = actor_center_x[first_actor_idx] - actor_widths[first_actor_idx] / 2.0 - note_w - SEQ_NOTE_GAP;
        } else if (note->position == NIXIE_SEQ_NOTE_RIGHT) {
            note_x = actor_center_x[first_actor_idx] + actor_widths[first_actor_idx] / 2.0 + SEQ_NOTE_GAP;
        } else if (note->actor_count > 1) {
            size_t last_actor_idx = (size_t)note->actor_indices[note->actor_count - 1];
            note_x = (actor_center_x[first_actor_idx] + actor_center_x[last_actor_idx]) / 2.0 - note_w / 2.0;
        } else {
            note_x = actor_center_x[first_actor_idx] - note_w / 2.0;
        }

        nixie_ps_note_t *pn = &notes[(*note_out_count)++];
        pn->text = note->text;
        pn->x = note_x;
        pn->y = note_y;
        pn->width = note_w;
        pn->height = note_h;
        pn->position = note->position;
        pn->actor_count = note->actor_count;
        if (note->actor_count > 0) {
            const char **actor_ids = (const char **)nixie_arena_alloc(arena, note->actor_count * sizeof(char *));
            for (size_t ai = 0; ai < note->actor_count; ai++) {
                actor_ids[ai] = diagram->actors[(size_t)note->actor_indices[ai]].id;
            }
            pn->actors = actor_ids;
        } else {
            pn->actors = NULL;
        }

        note_y += note_h + 4.0;
    }

    *note_y_inout = note_y;
    return any;
}

static void act_stack_push(nixie_arena_t *arena, act_stack_t *s, double start_y, int depth) {
    if (s->count == s->cap) {
        size_t new_cap = s->cap == 0 ? 4 : s->cap * 2;
        double *new_y = (double *)nixie_arena_alloc(arena, new_cap * sizeof(double));
        int *new_d = (int *)nixie_arena_alloc(arena, new_cap * sizeof(int));
        if (s->count > 0) {
            memcpy(new_y, s->start_y, s->count * sizeof(double));
            memcpy(new_d, s->depth, s->count * sizeof(int));
        }
        s->start_y = new_y;
        s->depth = new_d;
        s->cap = new_cap;
    }
    s->start_y[s->count] = start_y;
    s->depth[s->count] = depth;
    s->count++;
}

nixie_positioned_sequence_diagram_t *nixie_sequence_layout(nixie_arena_t *arena, const nixie_seq_diagram_t *diagram) {
    nixie_positioned_sequence_diagram_t *out =
        (nixie_positioned_sequence_diagram_t *)nixie_arena_alloc_zeroed(arena, sizeof(nixie_positioned_sequence_diagram_t));

    size_t actor_count = diagram->actor_count;
    if (actor_count == 0) {
        out->width = 0;
        out->height = 0;
        return out;
    }

    /* --- 1. Actor widths + center-X positions --- */

    double *actor_widths = (double *)nixie_arena_alloc(arena, actor_count * sizeof(double));
    for (size_t i = 0; i < actor_count; i++) {
        double text_w = nixie_measure_text_width(diagram->actors[i].label, FONT_SIZE_NODE_LABEL, FONT_WEIGHT_NODE_LABEL);
        actor_widths[i] = dmax(text_w + SEQ_ACTOR_PAD_X * 2, 80.0);
    }

    double *actor_center_x = (double *)nixie_arena_alloc(arena, actor_count * sizeof(double));
    double current_x = SEQ_PADDING + actor_widths[0] / 2.0;
    actor_center_x[0] = current_x;
    for (size_t i = 1; i < actor_count; i++) {
        double min_gap = dmax(SEQ_ACTOR_GAP, (actor_widths[i - 1] + actor_widths[i]) / 2.0 + 40.0);
        current_x += min_gap;
        actor_center_x[i] = current_x;
    }

    /* --- 2. Position actors at the top --- */

    double actor_y = SEQ_PADDING;
    nixie_ps_actor_t *actors = (nixie_ps_actor_t *)nixie_arena_alloc(arena, actor_count * sizeof(nixie_ps_actor_t));
    for (size_t i = 0; i < actor_count; i++) {
        actors[i].id = diagram->actors[i].id;
        actors[i].label = diagram->actors[i].label;
        actors[i].type = diagram->actors[i].type;
        actors[i].x = actor_center_x[i];
        actors[i].y = actor_y;
        actors[i].width = actor_widths[i];
        actors[i].height = SEQ_ACTOR_HEIGHT;
    }

    /* --- 3. Stack messages vertically --- */

    size_t message_count = diagram->message_count;
    double message_y = actor_y + SEQ_ACTOR_HEIGHT + SEQ_HEADER_GAP;

    nixie_ps_message_t *messages =
        message_count > 0 ? (nixie_ps_message_t *)nixie_arena_alloc(arena, message_count * sizeof(nixie_ps_message_t)) : NULL;

    /* Upper bound: every activate=true message eventually emits exactly one
     * activation (either via a matching deactivate, or -- if never closed
     * -- via the unclosed-activations pass after the loop below). */
    nixie_ps_activation_t *activations =
        message_count > 0 ? (nixie_ps_activation_t *)nixie_arena_alloc(arena, message_count * sizeof(nixie_ps_activation_t)) : NULL;
    size_t activation_count = 0;

    act_stack_t *stacks = (act_stack_t *)nixie_arena_alloc_zeroed(arena, actor_count * sizeof(act_stack_t));

    nixie_ps_note_t *notes = diagram->note_count > 0
                                  ? (nixie_ps_note_t *)nixie_arena_alloc(arena, diagram->note_count * sizeof(nixie_ps_note_t))
                                  : NULL;
    size_t note_out_count = 0;

    /* A note written before the first message (after_index == -1) -- see
     * position_notes_for()'s doc comment on why this needs handling
     * outside the per-message loop below. Anchored the same way the
     * in-loop case anchors off a message's row (+8 breathing room), just
     * using the initial message_y (right after the actor header gap) as
     * the timeline position instead of a specific message's y. */
    {
        double note_y = message_y + 8.0;
        if (position_notes_for(arena, diagram, actor_center_x, actor_widths, -1, &note_y, notes, &note_out_count)) {
            message_y = dmax(message_y, note_y + SEQ_MESSAGE_ROW_HEIGHT / 2.0);
        }
    }

    for (size_t msg_idx = 0; msg_idx < message_count; msg_idx++) {
        const nixie_seq_message_t *msg = &diagram->messages[msg_idx];
        int is_self = msg->from_idx == msg->to_idx;

        /* Extra vertical space if this message sits below a block header
         * or divider (scanned linearly -- block/divider counts are small
         * in practice, so this avoids needing an index-keyed map). */
        double extra = 0.0;
        for (size_t b = 0; b < diagram->block_count; b++) {
            const nixie_seq_block_t *blk = &diagram->blocks[b];
            if (blk->start_index == (int)msg_idx) extra = dmax(extra, SEQ_BLOCK_HEADER_EXTRA);
            for (size_t dv = 0; dv < blk->divider_count; dv++) {
                if (blk->dividers[dv].index == (int)msg_idx) extra = dmax(extra, SEQ_DIVIDER_EXTRA);
            }
        }
        if (extra > 0.0) message_y += extra;

        double x1 = actor_center_x[(size_t)msg->from_idx];
        double x2 = actor_center_x[(size_t)msg->to_idx];

        nixie_ps_message_t *pm = &messages[msg_idx];
        pm->from = diagram->actors[msg->from_idx].id;
        pm->to = diagram->actors[msg->to_idx].id;
        pm->label = msg->label;
        pm->line_style = msg->line_style;
        pm->arrow_head = msg->arrow_head;
        pm->x1 = x1;
        pm->x2 = x2;
        pm->y = message_y;
        pm->is_self = is_self;

        if (msg->activate) {
            act_stack_t *s = &stacks[(size_t)msg->to_idx];
            int depth = (int)s->count;
            act_stack_push(arena, s, message_y, depth);
        }

        if (msg->deactivate) {
            act_stack_t *s = &stacks[(size_t)msg->from_idx];
            if (s->count > 0) {
                s->count--;
                double start_y = s->start_y[s->count];
                int depth = s->depth[s->count];
                double x_offset = (double)depth * SEQ_NESTING_OFFSET;
                nixie_ps_activation_t *act = &activations[activation_count++];
                act->actor_id = diagram->actors[msg->from_idx].id;
                act->x = actor_center_x[(size_t)msg->from_idx] - SEQ_ACTIVATION_WIDTH / 2.0 + x_offset;
                act->top_y = start_y;
                act->bottom_y = message_y;
                act->width = SEQ_ACTIVATION_WIDTH;
            }
        }

        message_y += is_self ? SEQ_SELF_MESSAGE_HEIGHT + SEQ_MESSAGE_ROW_HEIGHT : SEQ_MESSAGE_ROW_HEIGHT;

        /* Notes that appear after this message. */
        double self_loop_extra = is_self ? SEQ_SELF_MESSAGE_HEIGHT : 0.0;
        double note_y = pm->y + self_loop_extra + 8.0;
        int any_note_here =
            position_notes_for(arena, diagram, actor_center_x, actor_widths, (int)msg_idx, &note_y, notes, &note_out_count);

        if (any_note_here) {
            message_y = dmax(message_y, note_y + SEQ_MESSAGE_ROW_HEIGHT / 2.0);
        }
    }

    /* Close any unclosed activations (preserving depth for the x-offset). */
    for (size_t i = 0; i < actor_count; i++) {
        act_stack_t *s = &stacks[i];
        for (size_t k = 0; k < s->count; k++) {
            double x_offset = (double)s->depth[k] * SEQ_NESTING_OFFSET;
            nixie_ps_activation_t *act = &activations[activation_count++];
            act->actor_id = diagram->actors[i].id;
            act->x = actor_center_x[i] - SEQ_ACTIVATION_WIDTH / 2.0 + x_offset;
            act->top_y = s->start_y[k];
            act->bottom_y = message_y - SEQ_MESSAGE_ROW_HEIGHT / 2.0;
            act->width = SEQ_ACTIVATION_WIDTH;
        }
    }

    /* --- 4. Position blocks (loop/alt/opt/...) --- */

    nixie_ps_block_t *blocks =
        diagram->block_count > 0 ? (nixie_ps_block_t *)nixie_arena_alloc(arena, diagram->block_count * sizeof(nixie_ps_block_t)) : NULL;

    for (size_t bi = 0; bi < diagram->block_count; bi++) {
        const nixie_seq_block_t *block = &diagram->blocks[bi];
        nixie_ps_block_t *pb = &blocks[bi];

        int start_valid = block->start_index >= 0 && block->start_index < (int)message_count;
        int end_valid = block->end_index >= 0 && block->end_index < (int)message_count;
        double block_top = (start_valid ? messages[block->start_index].y : message_y) - SEQ_BLOCK_PAD_TOP;
        double block_bottom = (end_valid ? messages[block->end_index].y : message_y) + SEQ_BLOCK_PAD_BOTTOM + 12.0;

        int min_idx = -1, max_idx = -1;
        for (int mi = block->start_index; mi <= block->end_index; mi++) {
            if (mi < 0 || mi >= (int)message_count) continue;
            const nixie_seq_message_t *m = &diagram->messages[mi];
            int lo = m->from_idx < m->to_idx ? m->from_idx : m->to_idx;
            int hi = m->from_idx > m->to_idx ? m->from_idx : m->to_idx;
            if (min_idx < 0 || lo < min_idx) min_idx = lo;
            if (max_idx < 0 || hi > max_idx) max_idx = hi;
        }
        if (min_idx < 0) {
            min_idx = 0;
            max_idx = (int)actor_count - 1;
        }

        double block_left = actor_center_x[(size_t)min_idx] - actor_widths[(size_t)min_idx] / 2.0 - SEQ_BLOCK_PAD_X;
        double block_right = actor_center_x[(size_t)max_idx] + actor_widths[(size_t)max_idx] / 2.0 + SEQ_BLOCK_PAD_X;

        pb->type = block->type;
        pb->label = block->label;
        pb->x = block_left;
        pb->y = block_top;
        pb->width = block_right - block_left;
        pb->height = block_bottom - block_top;

        pb->divider_count = block->divider_count;
        pb->dividers = block->divider_count > 0
                           ? (nixie_ps_divider_t *)nixie_arena_alloc(arena, block->divider_count * sizeof(nixie_ps_divider_t))
                           : NULL;
        for (size_t di = 0; di < block->divider_count; di++) {
            const nixie_seq_divider_t *d = &block->dividers[di];
            int d_valid = d->index >= 0 && d->index < (int)message_count;
            double msg_y = d_valid ? messages[d->index].y : message_y;
            double offset = 28.0;

            if (d->label[0] != '\0' && d_valid && messages[d->index].label[0] != '\0') {
                const nixie_ps_message_t *msg = &messages[d->index];
                nixie_strbuf_t sb;
                nixie_strbuf_init(&sb);
                nixie_strbuf_append_char(&sb, '[');
                nixie_strbuf_append(&sb, d->label);
                nixie_strbuf_append_char(&sb, ']');
                double div_label_w = nixie_measure_text_width(sb.data, FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL);
                nixie_strbuf_free(&sb);

                double div_label_left = block_left + 8.0;
                double div_label_right = div_label_left + div_label_w;

                double msg_label_w = nixie_measure_text_width(msg->label, FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL);
                double msg_label_left =
                    msg->is_self ? msg->x1 + 36.0 : (msg->x1 + msg->x2) / 2.0 - msg_label_w / 2.0;
                double msg_label_right = msg_label_left + msg_label_w;

                if (div_label_right > msg_label_left && div_label_left < msg_label_right) {
                    offset = 36.0;
                }
            }

            pb->dividers[di].y = msg_y - offset;
            pb->dividers[di].label = d->label;
        }
    }

    /* --- 6. Bounding-box post-processing: compute extents, shift right if
     * anything (typically a "left of" note on the first actor) extends
     * past the left padding margin. --- */

    double diagram_bottom = message_y + SEQ_PADDING;

    double global_min_x = SEQ_PADDING;
    double global_max_x = 0.0;
    for (size_t i = 0; i < actor_count; i++) {
        global_min_x = dmin(global_min_x, actors[i].x - actors[i].width / 2.0);
        global_max_x = dmax(global_max_x, actors[i].x + actors[i].width / 2.0);
    }
    for (size_t i = 0; i < diagram->block_count; i++) {
        global_min_x = dmin(global_min_x, blocks[i].x);
        global_max_x = dmax(global_max_x, blocks[i].x + blocks[i].width);
    }
    for (size_t i = 0; i < note_out_count; i++) {
        global_min_x = dmin(global_min_x, notes[i].x);
        global_max_x = dmax(global_max_x, notes[i].x + notes[i].width);
    }
    for (size_t i = 0; i < message_count; i++) {
        if (messages[i].is_self && messages[i].label[0] != '\0') {
            double loop_w = 30.0, label_padding = 8.0;
            double label_left = messages[i].x1 + loop_w + label_padding;
            double label_width = nixie_measure_text_width(messages[i].label, FONT_SIZE_EDGE_LABEL, FONT_WEIGHT_EDGE_LABEL);
            global_max_x = dmax(global_max_x, label_left + label_width + 8.0);
        }
    }

    double shift_x = global_min_x < SEQ_PADDING ? SEQ_PADDING - global_min_x : 0.0;
    if (shift_x > 0.0) {
        for (size_t i = 0; i < actor_count; i++) actors[i].x += shift_x;
        for (size_t i = 0; i < message_count; i++) {
            messages[i].x1 += shift_x;
            messages[i].x2 += shift_x;
        }
        for (size_t i = 0; i < activation_count; i++) activations[i].x += shift_x;
        for (size_t i = 0; i < diagram->block_count; i++) blocks[i].x += shift_x;
        for (size_t i = 0; i < note_out_count; i++) notes[i].x += shift_x;
        for (size_t i = 0; i < actor_count; i++) actor_center_x[i] += shift_x;
    }

    /* --- 7. Lifelines (after the shift, so x matches the shifted actors) --- */

    nixie_ps_lifeline_t *lifelines = (nixie_ps_lifeline_t *)nixie_arena_alloc(arena, actor_count * sizeof(nixie_ps_lifeline_t));
    for (size_t i = 0; i < actor_count; i++) {
        lifelines[i].actor_id = diagram->actors[i].id;
        lifelines[i].x = actor_center_x[i];
        lifelines[i].top_y = actor_y + SEQ_ACTOR_HEIGHT;
        lifelines[i].bottom_y = diagram_bottom - SEQ_PADDING;
    }

    /* --- 8. Final dimensions --- */

    double diagram_width = global_max_x + shift_x + SEQ_PADDING;
    double diagram_height = diagram_bottom;

    out->width = dmax(diagram_width, 200.0);
    out->height = dmax(diagram_height, 100.0);
    out->actors = actors;
    out->actor_count = actor_count;
    out->lifelines = lifelines;
    out->lifeline_count = actor_count;
    out->messages = messages;
    out->message_count = message_count;
    out->activations = activations;
    out->activation_count = activation_count;
    out->blocks = blocks;
    out->block_count = diagram->block_count;
    out->notes = notes;
    out->note_count = note_out_count;

    return out;
}
