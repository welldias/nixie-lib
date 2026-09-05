add_executable(nixie_tests
    main.c
)

# The parser/layout test groups exercise internals (nixie_flowchart_parse,
# nixie_flowchart_layout, ...) that aren't part of the public <nixie/nixie.h>
# API, so the test binary needs the src/ headers and must link against the
# static library specifically: a shared library built without explicit
# export declarations for internal symbols may not expose them on all
# platforms (notably Windows), whereas a static library always does.
target_include_directories(nixie_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(NIXIE_BUILD_STATIC)
    target_link_libraries(nixie_tests PRIVATE nixie_static nixie_warnings nixie_sanitizers)
else()
    target_link_libraries(nixie_tests PRIVATE nixie nixie_warnings nixie_sanitizers)
endif()

target_compile_definitions(nixie_tests PRIVATE NIXIE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")

foreach(nixie_test_group parser state class xychart er sequence layout render_svg render_ascii png fixtures)
    add_test(NAME ${nixie_test_group} COMMAND nixie_tests ${nixie_test_group})
endforeach()
