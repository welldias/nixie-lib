#include "png_encode.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

unsigned char *nixie_png_encode(const nixie_canvas_t *canvas, size_t *out_size) {
    int len = 0;
    unsigned char *png = stbi_write_png_to_mem(canvas->pixels, canvas->w * 4, canvas->w, canvas->h, 4, &len);
    if (png == NULL) {
        if (out_size != NULL) *out_size = 0;
        return NULL;
    }
    if (out_size != NULL) *out_size = (size_t)len;
    return png;
}
