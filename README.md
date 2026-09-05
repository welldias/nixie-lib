# nixie

`nixie` is a small, dependency-free C99 library that parses Mermaid-style
diagram text and renders it to **SVG**, **PNG**, or **ASCII/Unicode art**.

It supports the following Mermaid diagram types:

- Flowcharts (`graph` / `flowchart`)
- State diagrams (`stateDiagram` / `stateDiagram-v2`)
- Sequence diagrams (`sequenceDiagram`)
- Class diagrams (`classDiagram`)
- Entity-relationship diagrams (`erDiagram`)
- XY charts (`xychart-beta`)


## Building

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

This produces `libnixie.a` / `libnixie.so` under `build/src/`, plus the
`nixie-tool` CLI and test suite (both can be disabled with
`-DNIXIE_BUILD_EXAMPLES=OFF` / `-DNIXIE_BUILD_TESTS=OFF`).

## Integrating into another project

The simplest way to consume `nixie` from another CMake project is
`add_subdirectory`:

```cmake
# In your project's CMakeLists.txt
add_subdirectory(third_party/nixie-lib)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE nixie)
```

`nixie` here is an ALIAS target that resolves to the shared library when
built, falling back to the static one otherwise. `#include <nixie/nixie.h>`
becomes available automatically since the library exports its `include/`
directory as a public include path.

If you're not using CMake, link directly against the built library instead:

```bash
cc my_app.c -I/path/to/nixie-lib/include -L/path/to/nixie-lib/build/src -lnixie -lm -o my_app
```

## Usage example: rendering SVG

```c
#include <nixie/nixie.h>
#include <stdio.h>

int main(void) {
    const char *diagram =
        "graph TD\n"
        "  A[Start] --> B{Decision}\n"
        "  B -->|Yes| C[Do thing]\n"
        "  B -->|No| D[Other]\n";

    nixie_render_options_t opts = nixie_render_options_default();
    opts.theme = NIXIE_THEME_GITHUB_DARK;
    opts.transparent = 1;
    opts.use_unicode = 1;

    nixie_result_t result = nixie_render_svg(diagram, &opts);
    if (result.error != NIXIE_OK) {
        fprintf(stderr, "render error: %s\n", result.error_message);
        return 1;
    }

    fputs(result.output, stdout); /* a self-contained <svg>...</svg> string */
    nixie_free(result.output);
    return 0;
}
```

`opts` may also be `NULL` to use the defaults. The same `diagram` text can
be rendered to ASCII art with `nixie_render_ascii()`, or to a PNG image with
`nixie_render_png()` (which additionally requires `opts.font_path` to point
to a `.ttf`/`.otf` file, since nixie does not embed a font):

```c
nixie_render_options_t opts = nixie_render_options_default();
opts.font_path = "deafult.ttf";

nixie_png_result_t png = nixie_render_png(diagram, &opts);
if (png.error == NIXIE_OK) {
    FILE *f = fopen("diagram.png", "wb");
    fwrite(png.data, 1, png.size, f);
    fclose(f);
    nixie_free_png(png.data);
}
```

See `include/nixie/nixie.h` for the full API, and `example/nixie-tool.c` for
a complete CLI wrapper (`nixie-tool file.mmd --svg|--ascii|--png ...`).

## Acknowledgments

`nixie` was heavily inspired by [beautiful-mermaid](https://github.com/lukilabs/beautiful-mermaid)
and [nanosvg](https://github.com/memononen/nanosvg). The author is immensely
grateful to these projects and their creators.

PNG output is built on the [stb](https://github.com/nothings/stb) single-header
libraries (`stb_truetype.h` and `stb_image_write.h`). Many thanks to Sean
Barrett and the stb project for making them freely available.
