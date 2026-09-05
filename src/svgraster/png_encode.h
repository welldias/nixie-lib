#ifndef NIXIE_SVGRASTER_PNG_ENCODE_H
#define NIXIE_SVGRASTER_PNG_ENCODE_H

#include <stddef.h>

#include "canvas.h"

/*
 * Encodes `canvas` as PNG bytes. Returns a malloc'd buffer the caller frees
 * with plain free() (matching nixie_free_png()'s implementation), or NULL
 * on encode failure.
 */
unsigned char *nixie_png_encode(const nixie_canvas_t *canvas, size_t *out_size);

#endif /* NIXIE_SVGRASTER_PNG_ENCODE_H */
