#ifndef NIXIE_CLASS_RENDER_SVG_H
#define NIXIE_CLASS_RENDER_SVG_H

#include "../theme.h"
#include "positioned.h"

/* Returns a malloc'd SVG string; free with nixie_free() (== free()). */
char *nixie_class_render_svg(const nixie_positioned_class_diagram_t *pcd, const nixie_resolved_colors_t *colors, int transparent);

#endif /* NIXIE_CLASS_RENDER_SVG_H */
