#ifndef NIXIE_ER_LAYOUT_H
#define NIXIE_ER_LAYOUT_H

#include "../arena.h"
#include "model.h"
#include "positioned.h"

/* Formats an attribute as its display string ("KEYS type name"), used both
 * for width estimation (layout.c) and by the ASCII renderer's flat-text box
 * rows. */
char *nixie_er_attribute_to_string(nixie_arena_t *arena, const nixie_er_attribute_t *attr);

nixie_positioned_er_diagram_t *nixie_er_layout(nixie_arena_t *arena, const nixie_er_diagram_t *diagram);

#endif /* NIXIE_ER_LAYOUT_H */
