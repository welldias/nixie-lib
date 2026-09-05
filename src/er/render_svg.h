#ifndef NIXIE_ER_RENDER_SVG_H
#define NIXIE_ER_RENDER_SVG_H

#include "../theme.h"
#include "positioned.h"

/* Returns a malloc'd SVG string; free with nixie_free() (== free()). */
char *nixie_er_render_svg(
    const nixie_positioned_er_diagram_t *pcd, const nixie_resolved_colors_t *colors, int transparent);

#endif /* NIXIE_ER_RENDER_SVG_H */
