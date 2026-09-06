#ifndef NIXIE_FLOWCHART_RENDER_ASCII_H
#define NIXIE_FLOWCHART_RENDER_ASCII_H

#include "../arena.h"
#include "positioned.h"

typedef struct nixie_ascii_options {
    int use_unicode; /* 1 = box-drawing glyphs (e.g. U+250C), 0 = plain ASCII */
} nixie_ascii_options_t;

/*
 * Renders a positioned flowchart as ASCII/Unicode text art.
 *
 * Unlike render_svg.c, this does NOT reuse the pixel-space edge polylines
 * from the positioned model. Instead it regroups nodes by their `layer`
 * (see positioned.h) into character-grid rows/columns sized from each
 * label's actual character content, then re-routes each edge directly
 * between the resulting character boxes with a small orthogonal
 * exit/elbow/entry routine (mirroring layout_layered.c's pixel-space
 * routing, but in grid units). See the project plan's "ASCII layout
 * strategy" section for why this single-shared-model, no-independent-A*
 * design was chosen over porting beautiful-mermaid's separate grid+A*
 * ASCII backend.
 *
 * Returns a malloc'd string; free with nixie_free() (== free()).
 */
char *nixie_flowchart_render_ascii(nixie_arena_t *arena, const nixie_positioned_flowchart_t *pf, const nixie_ascii_options_t *opts);

#endif /* NIXIE_FLOWCHART_RENDER_ASCII_H */
