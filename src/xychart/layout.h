#ifndef NIXIE_XYCHART_LAYOUT_H
#define NIXIE_XYCHART_LAYOUT_H

#include <stddef.h>

#include "../arena.h"
#include "model.h"
#include "positioned.h"

/* Shared helpers -- also used directly by render_ascii.c, which lays out
 * its own character-grid chart rather than reusing pixel coordinates. */
size_t nixie_xy_get_data_count(const nixie_xy_chart_t *chart);
char **nixie_xy_get_category_labels(nixie_arena_t *arena, const nixie_xy_chart_t *chart, size_t count);
double *nixie_xy_nice_tick_values(nixie_arena_t *arena, double min, double max, size_t *out_count);
void nixie_xy_format_tick_value(double v, char *buf, size_t buflen);

nixie_positioned_xy_chart_t *nixie_xy_layout(nixie_arena_t *arena, const nixie_xy_chart_t *chart);

#endif /* NIXIE_XYCHART_LAYOUT_H */
