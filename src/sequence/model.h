#ifndef NIXIE_SEQUENCE_MODEL_H
#define NIXIE_SEQUENCE_MODEL_H

#include <stddef.h>

#include "../strmap.h"

typedef enum nixie_seq_actor_type {
    NIXIE_SEQ_PARTICIPANT,
    NIXIE_SEQ_ACTOR /* stick-figure icon instead of a box */
} nixie_seq_actor_type_t;

typedef struct nixie_seq_actor {
    char *id;
    char *label; /* defaults to id */
    nixie_seq_actor_type_t type;
} nixie_seq_actor_t;

typedef enum nixie_seq_line_style {
    NIXIE_SEQ_SOLID,
    NIXIE_SEQ_DASHED
} nixie_seq_line_style_t;

typedef enum nixie_seq_arrow_head {
    NIXIE_SEQ_OPEN,
    NIXIE_SEQ_FILLED
} nixie_seq_arrow_head_t;

typedef struct nixie_seq_message {
    int from_idx;
    int to_idx;
    char *label;
    nixie_seq_line_style_t line_style;
    nixie_seq_arrow_head_t arrow_head;
    int activate;   /* '+' on the target */
    int deactivate; /* '-' on the target */
} nixie_seq_message_t;

typedef enum nixie_seq_block_type {
    NIXIE_SEQ_BLOCK_LOOP,
    NIXIE_SEQ_BLOCK_ALT,
    NIXIE_SEQ_BLOCK_OPT,
    NIXIE_SEQ_BLOCK_PAR,
    NIXIE_SEQ_BLOCK_CRITICAL,
    NIXIE_SEQ_BLOCK_BREAK,
    NIXIE_SEQ_BLOCK_RECT
} nixie_seq_block_type_t;

typedef struct nixie_seq_divider {
    int index;   /* message index this divider (else/and) precedes */
    char *label; /* "" if none */
} nixie_seq_divider_t;

typedef struct nixie_seq_block {
    nixie_seq_block_type_t type;
    char *label; /* "" if none */
    int start_index;
    int end_index;

    nixie_seq_divider_t *dividers;
    size_t divider_count;
    size_t divider_cap;
} nixie_seq_block_t;

typedef enum nixie_seq_note_position {
    NIXIE_SEQ_NOTE_LEFT,
    NIXIE_SEQ_NOTE_RIGHT,
    NIXIE_SEQ_NOTE_OVER
} nixie_seq_note_position_t;

typedef struct nixie_seq_note {
    int *actor_indices;
    size_t actor_count;
    size_t actor_cap;

    char *text;
    nixie_seq_note_position_t position;
    int after_index; /* index of the message this note follows, -1 = before any message */
} nixie_seq_note_t;

typedef struct nixie_seq_diagram {
    nixie_seq_actor_t *actors;
    size_t actor_count;
    size_t actor_cap;

    nixie_seq_message_t *messages;
    size_t message_count;
    size_t message_cap;

    nixie_seq_block_t *blocks;
    size_t block_count;
    size_t block_cap;

    nixie_seq_note_t *notes;
    size_t note_count;
    size_t note_cap;

    nixie_strmap_t *actor_index; /* id -> index in actors[] */
} nixie_seq_diagram_t;

#endif /* NIXIE_SEQUENCE_MODEL_H */
