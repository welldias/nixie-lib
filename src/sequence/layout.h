#ifndef NIXIE_SEQUENCE_LAYOUT_H
#define NIXIE_SEQUENCE_LAYOUT_H

#include "../arena.h"
#include "model.h"
#include "positioned.h"

nixie_positioned_sequence_diagram_t *nixie_sequence_layout(nixie_arena_t *arena, const nixie_seq_diagram_t *diagram);

#endif /* NIXIE_SEQUENCE_LAYOUT_H */
