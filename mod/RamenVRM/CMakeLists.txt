set(TARGET RamenVRM)

add_library(${TARGET} SHARED
    src/dllmain.cpp
    src/custom_materials.cpp
    src/log.cpp
    src/material_finder.cpp
    src/settings.cpp
    src/steam_presence.cpp
    src/ue_call.cpp
    src/vrm_character.cpp
)
target_include_directories(${TARGET} PRIVATE src)
target_link_libraries(${TARGET} PRIVATE rvrm_core shell32 ole32)
target_link_libraries(${TARGET} PUBLIC UE4SS)
target_compile_options(${TARGET} PRIVATE /utf-8)
# UE4SS が読むファイル名は main.dll
set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME main)
target_link_options(${TARGET} PRIVATE /MAP)
