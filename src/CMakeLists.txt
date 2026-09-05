set(NIXIE_SOURCES
    nixie.c
    arena.c
    strbuf.c
    strmap.c
    color.c
    theme.c
    text_metrics.c
    diagram_type.c
    layout_layered.c
    flowchart/graph_build.c
    flowchart/parser.c
    flowchart/layout.c
    flowchart/render_svg.c
    flowchart/render_ascii.c
    state/parser.c
    class/parser.c
    class/layout.c
    class/render_svg.c
    class/render_ascii.c
    xychart/parser.c
    xychart/layout.c
    xychart/colors.c
    xychart/render_svg.c
    xychart/render_ascii.c
    er/parser.c
    er/layout.c
    er/render_svg.c
    er/render_ascii.c
    sequence/parser.c
    sequence/layout.c
    sequence/render_svg.c
    sequence/render_ascii.c
    svgraster/svg_parse.c
    svgraster/svg_path.c
    svgraster/canvas.c
    svgraster/text.c
    svgraster/png_encode.c
    svgraster/svg_to_png.c
)

option(NIXIE_BUILD_STATIC "Build the static nixie library (libnixie.a)" ON)
option(NIXIE_BUILD_SHARED "Build the shared nixie library (libnixie.so)" ON)

# layout_layered.c/flowchart/layout.c/render_svg.c use sqrt()/fabs()/ceil().
if(UNIX)
    find_library(NIXIE_MATH_LIBRARY m)
endif()

if(NOT NIXIE_BUILD_STATIC AND NOT NIXIE_BUILD_SHARED)
    message(FATAL_ERROR "At least one of NIXIE_BUILD_STATIC or NIXIE_BUILD_SHARED must be ON")
endif()

if(NIXIE_BUILD_STATIC)
    add_library(nixie_static STATIC ${NIXIE_SOURCES})
    target_include_directories(nixie_static PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_include_directories(nixie_static PRIVATE ${CMAKE_SOURCE_DIR}/external/stb_image ${CMAKE_SOURCE_DIR}/external/stb_truetype)
    target_link_libraries(nixie_static PRIVATE nixie_warnings nixie_sanitizers)
    if(NIXIE_MATH_LIBRARY)
        target_link_libraries(nixie_static PRIVATE ${NIXIE_MATH_LIBRARY})
    endif()
    set_target_properties(nixie_static PROPERTIES OUTPUT_NAME nixie)
endif()

if(NIXIE_BUILD_SHARED)
    add_library(nixie_shared SHARED ${NIXIE_SOURCES})
    target_include_directories(nixie_shared PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_include_directories(nixie_shared PRIVATE ${CMAKE_SOURCE_DIR}/external/stb_image ${CMAKE_SOURCE_DIR}/external/stb_truetype)
    target_link_libraries(nixie_shared PRIVATE nixie_warnings nixie_sanitizers)
    if(NIXIE_MATH_LIBRARY)
        target_link_libraries(nixie_shared PRIVATE ${NIXIE_MATH_LIBRARY})
    endif()
    set_target_properties(nixie_shared PROPERTIES OUTPUT_NAME nixie)
endif()

# `nixie` is the default target other parts of the build (e.g. the example) link
# against: shared when available, falling back to static otherwise.
if(NIXIE_BUILD_SHARED)
    add_library(nixie ALIAS nixie_shared)
else()
    add_library(nixie ALIAS nixie_static)
endif()
