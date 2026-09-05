add_executable(nixie_tool
    nixie-tool.c
)

target_link_libraries(nixie_tool
    PRIVATE nixie nixie_warnings nixie_sanitizers
)

set_target_properties(nixie_tool PROPERTIES OUTPUT_NAME nixie-tool)
