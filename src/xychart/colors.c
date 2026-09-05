#include "colors.h"

#include <math.h>
#include <string.h>

static void rgb_to_hsl(nixie_rgb_t c, double *h, double *s, double *l) {
    double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
    double max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double lightness = (max + min) / 2.0;

    if (max == min) {
        *h = 0.0;
        *s = 0.0;
        *l = lightness * 100.0;
        return;
    }

    double d = max - min;
    double sat = lightness > 0.5 ? d / (2.0 - max - min) : d / (max + min);

    double hue;
    if (max == r) {
        hue = (fmod((g - b) / d + (g < b ? 6.0 : 0.0), 6.0)) / 6.0;
    } else if (max == g) {
        hue = ((b - r) / d + 2.0) / 6.0;
    } else {
        hue = ((r - g) / d + 4.0) / 6.0;
    }

    *h = hue * 360.0;
    *s = sat * 100.0;
    *l = lightness * 100.0;
}

static int round_clamp255(double v) {
    int r = (int)(v + 0.5);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    return r;
}

static nixie_rgb_t hsl_to_rgb(double h, double s, double l) {
    double si = s / 100.0, li = l / 100.0;
    double c = (1.0 - fabs(2.0 * li - 1.0)) * si;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double m = li - c / 2.0;
    double r, g, b;

    if (h < 60) { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else { r = c; g = 0; b = x; }

    nixie_rgb_t out;
    out.r = (unsigned char)round_clamp255((r + m) * 255.0);
    out.g = (unsigned char)round_clamp255((g + m) * 255.0);
    out.b = (unsigned char)round_clamp255((b + m) * 255.0);
    return out;
}

void nixie_xy_series_color(int index, const char *accent_hex, const char *bg_hex, char out[8]) {
    nixie_rgb_t rgb;
    int parsed = accent_hex != NULL && nixie_parse_hex(accent_hex, &rgb) == 0;
    if (!parsed) {
        nixie_parse_hex(NIXIE_CHART_ACCENT_FALLBACK, &rgb);
    }

    if (index == 0) {
        nixie_format_hex(rgb, out);
        return;
    }

    double h, s, l;
    rgb_to_hsl(rgb, &h, &s, &l);
    double chart_s = s;
    if (chart_s < 55.0) chart_s = 55.0;
    if (chart_s > 85.0) chart_s = 85.0;

    int tier = (index + 1) / 2;
    int odd_index = (index % 2) == 1;

    int dark_bg = 0;
    if (bg_hex != NULL) {
        nixie_rgb_t bg_rgb;
        if (nixie_parse_hex(bg_hex, &bg_rgb) == 0) {
            double bh, bs, bl;
            rgb_to_hsl(bg_rgb, &bh, &bs, &bl);
            (void)bh;
            (void)bs;
            dark_bg = bl < 50.0;
        }
    }

    int dark = dark_bg ? !odd_index : odd_index;

    double lightness;
    if (dark) {
        lightness = 48.0 - (double)tier * 13.0;
        if (lightness < 25.0) lightness = 25.0;
    } else {
        lightness = 55.0 + (double)tier * 11.0;
        if (lightness > 78.0) lightness = 78.0;
    }

    double h_shift = (dark ? -8.0 : 12.0) * (double)tier;
    double new_h = fmod(h + h_shift, 360.0);
    if (new_h < 0.0) new_h += 360.0;

    nixie_rgb_t result = hsl_to_rgb(new_h, chart_s, lightness);
    nixie_format_hex(result, out);
}
