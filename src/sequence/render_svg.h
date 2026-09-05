#ifndef NIXIE_SEQUENCE_RENDER_SVG_H
#define NIXIE_SEQUENCE_RENDER_SVG_H

#include "../theme.h"
#include "positioned.h"

/* Returns a malloc'd SVG string; free with nixie_free() (== free()). */
char *nixie_sequence_render_svg(
    const nixie_positioned_sequence_diagram_t *psd, const nixie_resolved_colors_t *colors, int transparent);

#endif /* NIXIE_SEQUENCE_RENDER_SVG_H */
