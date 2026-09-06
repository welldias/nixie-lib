#include <nixie/nixie.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { OUTPUT_NONE, OUTPUT_SVG, OUTPUT_ASCII, OUTPUT_PNG } output_mode_t;

nixie_render_options_t render_options;

static void print_usage(const char *prog) {
    fprintf(stderr, "usage: %s <input.mmd> (--svg | --ascii | --png <output.png> --font <font.ttf>) [--scale <n>]\n", prog);
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)len + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[nread] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    const char *input_path      = NULL;
    const char *png_output_path = NULL;
    const char *font_path       = NULL;
    double scale                = 1.0;
    output_mode_t mode          = OUTPUT_NONE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--svg") == 0) {
            mode = OUTPUT_SVG;
        } else if (strcmp(argv[i], "--ascii") == 0) {
            mode = OUTPUT_ASCII;
        } else if (strcmp(argv[i], "--png") == 0) {
            mode = OUTPUT_PNG;
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --png requires an output path\n", argv[0]);
                print_usage(argv[0]);
                return 1;
            }
            png_output_path = argv[++i];
        } else if (strcmp(argv[i], "--font") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --font requires a path\n", argv[0]);
                print_usage(argv[0]);
                return 1;
            }
            font_path = argv[++i];
        } else if (strcmp(argv[i], "--scale") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --scale requires a number\n", argv[0]);
                print_usage(argv[0]);
                return 1;
            }
            scale = atof(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (input_path == NULL) {
            input_path = argv[i];
        } else {
            fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (input_path == NULL || mode == OUTPUT_NONE) {
        print_usage(argv[0]);
        return 1;
    }
    if (mode == OUTPUT_PNG && (png_output_path == NULL || font_path == NULL)) {
        fprintf(stderr, "%s: --png requires both an output path and --font <font.ttf>\n", argv[0]);
        print_usage(argv[0]);
        return 1;
    }

    char *text = read_file(input_path);
    if (text == NULL) {
        fprintf(stderr, "%s: could not read '%s'\n", argv[0], input_path);
        return 1;
    }

    render_options.theme       = NIXIE_THEME_CATPPUCCIN_MOCHA;
    render_options.transparent = 1;
    render_options.use_unicode = 1;
    render_options.font_path   = font_path;
    render_options.scale       = (scale > 0.0) ? scale : 1.0;

    if (mode == OUTPUT_PNG) {
        nixie_png_result_t png = nixie_render_png(text, &render_options);
        free(text);

        if (png.error != NIXIE_OK) {
            fprintf(stderr, "%s: render error: %s", argv[0], png.error_message);
            if (png.error_line >= 0) {
                fprintf(stderr, " (line %d)", png.error_line);
            }
            fprintf(stderr, "\n");
            return 1;
        }

        FILE *out = fopen(png_output_path, "wb");
        if (out == NULL) {
            fprintf(stderr, "%s: could not open '%s' for writing\n", argv[0], png_output_path);
            nixie_free_png(png.data);
            return 1;
        }
        size_t written = fwrite(png.data, 1, png.size, out);
        fclose(out);
        nixie_free_png(png.data);
        if (written != png.size) {
            fprintf(stderr, "%s: short write to '%s'\n", argv[0], png_output_path);
            return 1;
        }
        return 0;
    }

    nixie_result_t result = (mode == OUTPUT_SVG) ? nixie_render_svg(text, &render_options) : nixie_render_ascii(text, &render_options);
    free(text);

    if (result.error != NIXIE_OK) {
        fprintf(stderr, "%s: render error: %s", argv[0], result.error_message);
        if (result.error_line >= 0) {
            fprintf(stderr, " (line %d)", result.error_line);
        }
        fprintf(stderr, "\n");
        return 1;
    }

    fputs(result.output, stdout);
    fputc('\n', stdout);
    nixie_free(result.output);

    return 0;
}
