#ifndef NIXIE_SVGRASTER_SVG_PARSE_H
#define NIXIE_SVGRASTER_SVG_PARSE_H

#include <nixie/nixie.h>

#include "../arena.h"
#include "svg_ir.h"

typedef struct nixie_svg_parse_result {
    nixie_svg_document_t *doc; /* NULL on error */
    nixie_error_t error;
    char error_message[256];
    int error_line;
} nixie_svg_parse_result_t;

/*
 * Parses `svg_text` (the bounded SVG subset nixie's own renderers emit --
 * see svg_ir.h) into a flat nixie_svg_document_t. All allocations come
 * from `arena`. `scale` (<= 0 treated as 1.0) is baked into every
 * coordinate/length in the resulting document, including the document's
 * own width/height, so everything downstream operates directly in final
 * device-pixel space.
 */
nixie_svg_parse_result_t nixie_svg_parse(nixie_arena_t *arena, const char *svg_text, double scale);

#endif /* NIXIE_SVGRASTER_SVG_PARSE_H */
