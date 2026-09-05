#ifndef NIXIE_CLASS_RENDER_ASCII_H
#define NIXIE_CLASS_RENDER_ASCII_H

#include "../arena.h"
#include "positioned.h"

typedef struct nixie_class_ascii_options {
    int use_unicode; /* 1 = box-drawing glyphs, 0 = plain ASCII */
} nixie_class_ascii_options_t;

/*
 * Renders a positioned class diagram as ASCII/Unicode text art: each class
 * becomes a 3-compartment box (header / attributes / methods), laid out on
 * a character grid grouped by `layer` (see positioned.h), with relationship
 * lines re-routed directly between the resulting boxes (mirroring
 * flowchart/render_ascii.c's approach, but without that renderer's
 * long-edge margin-channel routing or label-collision search -- class
 * hierarchies are overwhelmingly DAGs with short labels in practice, so
 * this simpler pass is accepted as this session's scope for the ASCII
 * backend).
 *
 * Returns a malloc'd string; free with nixie_free() (== free()).
 */
char *nixie_class_render_ascii(
    nixie_arena_t *arena, const nixie_positioned_class_diagram_t *pcd, const nixie_class_ascii_options_t *opts);

#endif /* NIXIE_CLASS_RENDER_ASCII_H */
