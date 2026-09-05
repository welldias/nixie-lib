#ifndef NIXIE_SEQUENCE_POSITIONED_H
#define NIXIE_SEQUENCE_POSITIONED_H

#include <stddef.h>

#include "../geometry.h"
#include "model.h"

typedef struct nixie_ps_actor {
    const char *id;
    const char *label;
    nixie_seq_actor_type_t type;
    double x, y, width, height; /* x is the actor's horizontal CENTER */
} nixie_ps_actor_t;

typedef struct nixie_ps_lifeline {
    const char *actor_id;
    double x, top_y, bottom_y;
} nixie_ps_lifeline_t;

typedef struct nixie_ps_message {
    const char *from;
    const char *to;
    const char *label;
    nixie_seq_line_style_t line_style;
    nixie_seq_arrow_head_t arrow_head;
    double x1, x2, y;
    int is_self;
} nixie_ps_message_t;

typedef struct nixie_ps_activation {
    const char *actor_id;
    double x, top_y, bottom_y, width;
} nixie_ps_activation_t;

typedef struct nixie_ps_divider {
    double y;
    const char *label; /* "" if none */
} nixie_ps_divider_t;

typedef struct nixie_ps_block {
    nixie_seq_block_type_t type;
    const char *label; /* "" if none */
    double x, y, width, height;
    nixie_ps_divider_t *dividers;
    size_t divider_count;
} nixie_ps_block_t;

typedef struct nixie_ps_note {
    const char *text;
    double x, y, width, height;
    nixie_seq_note_position_t position;
    const char **actors; /* actor id strings, for data-actors attributes */
    size_t actor_count;
} nixie_ps_note_t;

typedef struct nixie_positioned_sequence_diagram {
    double width;
    double height;

    nixie_ps_actor_t *actors;
    size_t actor_count;

    nixie_ps_lifeline_t *lifelines;
    size_t lifeline_count;

    nixie_ps_message_t *messages;
    size_t message_count;

    nixie_ps_activation_t *activations;
    size_t activation_count;

    nixie_ps_block_t *blocks;
    size_t block_count;

    nixie_ps_note_t *notes;
    size_t note_count;
} nixie_positioned_sequence_diagram_t;

#endif /* NIXIE_SEQUENCE_POSITIONED_H */
