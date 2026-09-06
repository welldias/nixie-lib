#ifndef NIXIE_FLOWCHART_RENDER_SVG_H
#define NIXIE_FLOWCHART_RENDER_SVG_H

#include "../theme.h"
#include "positioned.h"

/* Returns a malloc'd SVG string; free with nixie_free() (== free()). */
char *nixie_flowchart_render_svg(const nixie_positioned_flowchart_t *pf, const nixie_resolved_colors_t *colors, int transparent);

#endif /* NIXIE_FLOWCHART_RENDER_SVG_H */
