#ifndef NIXIE_CLASS_LAYOUT_H
#define NIXIE_CLASS_LAYOUT_H

#include "../arena.h"
#include "model.h"
#include "positioned.h"

/* Formats a member as its display string ("+name(params): type"), used both
 * for width estimation (layout.c) and by the ASCII renderer's flat-text box
 * rows. Ported from beautiful-mermaid/src/class/layout.ts's
 * memberToString(). */
char *nixie_class_member_to_string(nixie_arena_t *arena, const nixie_class_member_t *m);

nixie_positioned_class_diagram_t *nixie_class_layout(nixie_arena_t *arena, const nixie_class_diagram_t *diagram);

#endif /* NIXIE_CLASS_LAYOUT_H */
