#ifndef NIXIE_SEQUENCE_RENDER_ASCII_H
#define NIXIE_SEQUENCE_RENDER_ASCII_H

#include "../arena.h"
#include "positioned.h"

typedef struct nixie_sequence_ascii_options {
    int use_unicode; /* 1 = box-drawing glyphs, 0 = plain ASCII */
} nixie_sequence_ascii_options_t;

/*
 * Renders a positioned sequence diagram as ASCII/Unicode text art: a
 * column-per-actor, row-per-message layout mirroring
 * beautiful-mermaid/src/ascii/sequence.ts, computed independently from the
 * SVG pixel layout (character-grid units, not a pixel-to-character
 * projection of nixie_positioned_sequence_diagram_t) -- the same
 * architectural choice already made for xychart's ASCII backend. Actor
 * labels, normal message labels, note text and block headers support
 * multi-line text; self-message labels and divider ([else]/[and]) labels
 * are drawn as their first line only -- both are faithful reproductions of
 * ascii/sequence.ts's own asymmetric handling (it calls splitLines() for
 * the former group but indexes the raw string directly for the latter),
 * not a simplification introduced by this port.
 *
 * Returns a malloc'd string; free with nixie_free() (== free()).
 */
char *nixie_sequence_render_ascii(
    nixie_arena_t *arena, const nixie_seq_diagram_t *diagram, const nixie_sequence_ascii_options_t *opts);

#endif /* NIXIE_SEQUENCE_RENDER_ASCII_H */
