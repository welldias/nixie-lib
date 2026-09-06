#include "text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#define NIXIE_PI 3.14159265358979323846

nixie_font_t nixie_font_load(const char *font_path) {
    nixie_font_t font;
    font.file_data  = NULL;
    font.stbtt_info = NULL;
    font.ok         = 0;

    if (font_path == NULL) {
        return font;
    }

    FILE *f = fopen(font_path, "rb");
    if (f == NULL) {
        return font;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return font;
    }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return font;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)len);
    if (data == NULL) {
        fclose(f);
        return font;
    }
    size_t nread = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (nread != (size_t)len) {
        free(data);
        return font;
    }

    stbtt_fontinfo *info = (stbtt_fontinfo *)malloc(sizeof(stbtt_fontinfo));
    if (info == NULL) {
        free(data);
        return font;
    }
    if (!stbtt_InitFont(info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        free(info);
        free(data);
        return font;
    }

    font.file_data  = data;
    font.stbtt_info = info;
    font.ok         = 1;
    return font;
}

void nixie_font_free(nixie_font_t *font) {
    if (font == NULL) {
        return;
    }
    free(font->stbtt_info);
    free(font->file_data);
    font->stbtt_info = NULL;
    font->file_data  = NULL;
    font->ok         = 0;
}

static int utf8_decode(const char *s, int *out_cp) {
    unsigned char c0 = (unsigned char)s[0];
    if (c0 < 0x80) {
        *out_cp = c0;
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0 && s[1] != '\0') {
        *out_cp = ((c0 & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
        return 2;
    }
    if ((c0 & 0xF0) == 0xE0 && s[1] != '\0' && s[2] != '\0') {
        *out_cp = ((c0 & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F);
        return 3;
    }
    if ((c0 & 0xF8) == 0xF0 && s[1] != '\0' && s[2] != '\0' && s[3] != '\0') {
        *out_cp = ((c0 & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) | (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
        return 4;
    }
    *out_cp = c0;
    return 1;
}

static nixie_point_t rotate_point(nixie_point_t p, nixie_point_t center, double theta_rad) {
    double dx = p.x - center.x;
    double dy = p.y - center.y;
    double c  = cos(theta_rad);
    double s  = sin(theta_rad);
    nixie_point_t r;
    r.x = center.x + dx * c - dy * s;
    r.y = center.y + dx * s + dy * c;
    return r;
}

void nixie_draw_text(nixie_canvas_t *canvas, const nixie_font_t *font, const nixie_svg_shape_t *shape) {
    if (font == NULL || !font->ok || shape->text == NULL || shape->text[0] == '\0' || !shape->paint.has_fill) {
        return;
    }

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->stbtt_info;
    double scale         = stbtt_ScaleForPixelHeight(info, (float)shape->font_size);

    int ascent_i, descent_i, linegap_i;
    stbtt_GetFontVMetrics(info, &ascent_i, &descent_i, &linegap_i);
    (void)linegap_i;
    double ascent  = ascent_i * scale;
    double descent = descent_i * scale;

    int len  = (int)strlen(shape->text);
    int *cps = (int *)malloc(sizeof(int) * (size_t)len);
    if (cps == NULL) {
        return;
    }
    int cp_count = 0;
    {
        const char *p = shape->text;
        while (*p != '\0') {
            int cp;
            int n           = utf8_decode(p, &cp);
            cps[cp_count++] = cp;
            p += n;
        }
    }
    if (cp_count == 0) {
        free(cps);
        return;
    }

    double total_advance = 0.0;
    for (int i = 0; i < cp_count; i++) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, cps[i], &adv, &lsb);
        (void)lsb;
        total_advance += adv * scale;
        if (i + 1 < cp_count) {
            total_advance += stbtt_GetCodepointKernAdvance(info, cps[i], cps[i + 1]) * scale;
        }
    }

    double anchor_shift = 0.0;
    if (shape->text_anchor == NIXIE_SVG_ANCHOR_MIDDLE) {
        anchor_shift = -total_advance * 0.5;
    } else if (shape->text_anchor == NIXIE_SVG_ANCHOR_END) {
        anchor_shift = -total_advance;
    }

    int bold = (shape->font_weight >= 600) ? 1 : 0;

    double local_min_x = anchor_shift - 2.0;
    double local_max_x = anchor_shift + total_advance + 2.0 + bold;
    double local_min_y = -ascent - 2.0;
    double local_max_y = -descent + 2.0;
    if (shape->underline) {
        local_max_y += shape->font_size * 0.2;
    }

    int buf_x0 = (int)floor(local_min_x);
    int buf_y0 = (int)floor(local_min_y);
    int buf_w  = (int)ceil(local_max_x) - buf_x0 + 1;
    int buf_h  = (int)ceil(local_max_y) - buf_y0 + 1;
    if (buf_w <= 0 || buf_h <= 0) {
        free(cps);
        return;
    }

    unsigned char *buf = (unsigned char *)calloc((size_t)buf_w * (size_t)buf_h, 1);
    if (buf == NULL) {
        free(cps);
        return;
    }

    int passes = bold ? 2 : 1;
    for (int pass = 0; pass < passes; pass++) {
        double pen_x = anchor_shift + (pass == 1 ? 1.0 : 0.0);
        for (int i = 0; i < cp_count; i++) {
            int gw, gh, gxoff, gyoff;
            unsigned char *bitmap = stbtt_GetCodepointBitmap(info, (float)scale, (float)scale, cps[i], &gw, &gh, &gxoff, &gyoff);
            if (bitmap != NULL) {
                for (int by = 0; by < gh; by++) {
                    for (int bx = 0; bx < gw; bx++) {
                        unsigned char cov = bitmap[by * gw + bx];
                        if (cov == 0)
                            continue;
                        int lx = (int)floor(pen_x + gxoff + bx + 0.5) - buf_x0;
                        int ly = (int)floor(0.0 + gyoff + by + 0.5) - buf_y0;
                        if (lx < 0 || ly < 0 || lx >= buf_w || ly >= buf_h)
                            continue;
                        unsigned char *slot = &buf[ly * buf_w + lx];
                        if (cov > *slot)
                            *slot = cov;
                    }
                }
                stbtt_FreeBitmap(bitmap, NULL);
            }

            int adv, lsb;
            stbtt_GetCodepointHMetrics(info, cps[i], &adv, &lsb);
            (void)lsb;
            pen_x += adv * scale;
            if (i + 1 < cp_count) {
                pen_x += stbtt_GetCodepointKernAdvance(info, cps[i], cps[i + 1]) * scale;
            }
        }
    }
    free(cps);

    if (shape->underline) {
        double underline_y = shape->font_size * 0.08;
        double thickness   = shape->font_size * 0.06;
        if (thickness < 1.0)
            thickness = 1.0;
        int y0 = (int)floor(underline_y - thickness * 0.5) - buf_y0;
        int y1 = (int)ceil(underline_y + thickness * 0.5) - buf_y0;
        int x0 = (int)floor(anchor_shift) - buf_x0;
        int x1 = (int)ceil(anchor_shift + total_advance) - buf_x0;
        if (y0 < 0)
            y0 = 0;
        if (y1 > buf_h)
            y1 = buf_h;
        if (x0 < 0)
            x0 = 0;
        if (x1 > buf_w)
            x1 = buf_w;
        for (int yy = y0; yy < y1; yy++) {
            for (int xx = x0; xx < x1; xx++) {
                buf[yy * buf_w + xx] = 255;
            }
        }
    }

    nixie_rgb_t color = shape->paint.fill;
    double opacity    = shape->paint.opacity;

    if (!shape->has_rotation) {
        int origin_x = (int)floor(shape->text_x) + buf_x0;
        int origin_y = (int)floor(shape->text_y) + buf_y0;
        for (int ly = 0; ly < buf_h; ly++) {
            for (int lx = 0; lx < buf_w; lx++) {
                unsigned char cov = buf[ly * buf_w + lx];
                if (cov == 0)
                    continue;
                nixie_canvas_blend_pixel(canvas, origin_x + lx, origin_y + ly, color, (cov / 255.0) * opacity);
            }
        }
    } else {
        double theta                 = shape->rotate_deg * NIXIE_PI / 180.0;
        nixie_point_t center         = shape->rotate_center;
        nixie_point_t corners_abs[4] = {
            { shape->text_x + buf_x0,         shape->text_y + buf_y0         },
            { shape->text_x + buf_x0 + buf_w, shape->text_y + buf_y0         },
            { shape->text_x + buf_x0,         shape->text_y + buf_y0 + buf_h },
            { shape->text_x + buf_x0 + buf_w, shape->text_y + buf_y0 + buf_h }
        };

        double dminx = 1e30, dminy = 1e30, dmaxx = -1e30, dmaxy = -1e30;
        for (int i = 0; i < 4; i++) {
            nixie_point_t rp = rotate_point(corners_abs[i], center, theta);
            if (rp.x < dminx)
                dminx = rp.x;
            if (rp.x > dmaxx)
                dmaxx = rp.x;
            if (rp.y < dminy)
                dminy = rp.y;
            if (rp.y > dmaxy)
                dmaxy = rp.y;
        }

        int dst_x0 = (int)floor(dminx);
        int dst_y0 = (int)floor(dminy);
        int dst_x1 = (int)ceil(dmaxx);
        int dst_y1 = (int)ceil(dmaxy);
        if (dst_x0 < 0)
            dst_x0 = 0;
        if (dst_y0 < 0)
            dst_y0 = 0;
        if (dst_x1 > canvas->w)
            dst_x1 = canvas->w;
        if (dst_y1 > canvas->h)
            dst_y1 = canvas->h;

        for (int dy = dst_y0; dy < dst_y1; dy++) {
            for (int dx = dst_x0; dx < dst_x1; dx++) {
                nixie_point_t dst_center = { dx + 0.5, dy + 0.5 };
                nixie_point_t src        = rotate_point(dst_center, center, -theta);
                int lx                   = (int)floor(src.x - shape->text_x - buf_x0);
                int ly                   = (int)floor(src.y - shape->text_y - buf_y0);
                if (lx < 0 || ly < 0 || lx >= buf_w || ly >= buf_h)
                    continue;
                unsigned char cov = buf[ly * buf_w + lx];
                if (cov == 0)
                    continue;
                nixie_canvas_blend_pixel(canvas, dx, dy, color, (cov / 255.0) * opacity);
            }
        }
    }

    free(buf);
}
