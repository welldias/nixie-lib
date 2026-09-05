#ifndef NIXIE_ER_RENDER_ASCII_H
#define NIXIE_ER_RENDER_ASCII_H

#include "../arena.h"
#include "positioned.h"

typedef struct nixie_er_ascii_options {
    int use_unicode; /* 1 = box-drawing glyphs, 0 = plain ASCII */
} nixie_er_ascii_options_t;

/*
 * Renders a positioned ER diagram as ASCII/Unicode text art: each entity
 * becomes a 2-section box (header / attributes), laid out on a character
 * grid grouped by `layer` (see positioned.h; ER is always laid out
 * left-to-right, unlike class diagrams' top-down layout). Relationship
 * lines are re-routed directly between the resulting boxes, with a
 * crow's-foot glyph at each end (mirroring flowchart/render_ascii.c's
 * approach, but without that renderer's long-edge margin-channel routing --
 * see class/render_ascii.h's note on the same simplification for a
 * layout-engine-based diagram type).
 *
 * Returns a malloc'd string; free with nixie_free() (== free()).
 */
char *nixie_er_render_ascii(
    nixie_arena_t *arena, const nixie_positioned_er_diagram_t *pcd, const nixie_er_ascii_options_t *opts);

#endif /* NIXIE_ER_RENDER_ASCII_H */
