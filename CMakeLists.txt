cmake_minimum_required(VERSION 3.25)
project(HellVerdict VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(HV_ENABLE_VULKAN  "Vulkan renderer (primary)"    ON)
option(HV_ENABLE_OPENGL  "OpenGL 4.1 fallback"          ON)
option(HV_ENABLE_MONO    "C#/.NET 10 scripting"         ON)
option(HV_MOBILE         "Mobile/Android build"         OFF)

if(NOT DEFINED BALMUNG_DIR)
    set(BALMUNG_DIR "${CMAKE_SOURCE_DIR}/../balmung-engine" CACHE PATH "Balmung Engine root")
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /MP /utf-8
        $<$<CONFIG:Release>:/O2 /Ob2 /DNDEBUG /arch:AVX2>
        $<$<CONFIG:Debug>:/Od /Zi>)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter
        $<$<CONFIG:Release>:-O3 -DNDEBUG -march=native>
        $<$<CONFIG:Debug>:-g -O0>)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT HV_MOBILE)
        add_compile_options(-fsanitize=address,undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ── SDL2 ──────────────────────────────────────────────────────────────────────
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_library(SDL2_LIB SDL2 HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
    find_path(SDL2_INC SDL2/SDL.h HINTS /usr/include /usr/local/include)
    if(SDL2_LIB AND SDL2_INC)
        add_library(SDL2::SDL2 UNKNOWN IMPORTED)
        set_target_properties(SDL2::SDL2 PROPERTIES
            IMPORTED_LOCATION "${SDL2_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INC}")
    else()
        message(FATAL_ERROR "SDL2 not found.  sudo apt install libsdl2-dev")
    endif()
endif()

# ── Vulkan ────────────────────────────────────────────────────────────────────
if(HV_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if(NOT Vulkan_FOUND)
        message(WARNING "Vulkan SDK not found -- Vulkan disabled")
        set(HV_ENABLE_VULKAN OFF)
    else()
        message(STATUS "Vulkan: ${Vulkan_VERSION}")
    endif()
endif()

# ── OpenGL + GLAD ─────────────────────────────────────────────────────────────
if(HV_ENABLE_OPENGL)
    find_package(OpenGL QUIET)
    if(EXISTS "${CMAKE_SOURCE_DIR}/deps/glad/src/glad.c")
        add_library(glad STATIC deps/glad/src/glad.c)
        target_include_directories(glad PUBLIC deps/glad/include)
    endif()
endif()

# ── GLM (header-only) ─────────────────────────────────────────────────────────
find_path(GLM_INC glm/glm.hpp
    HINTS /usr/include /usr/local/include
          "${CMAKE_SOURCE_DIR}/deps/glm" "${BALMUNG_DIR}/deps/glm")
if(NOT GLM_INC)
    message(FATAL_ERROR "GLM not found.  sudo apt install libglm-dev")
endif()

# ── EnTT (header-only) ────────────────────────────────────────────────────────
find_path(ENTT_INC entt/entt.hpp
    HINTS /usr/include /usr/local/include
          "${CMAKE_SOURCE_DIR}/deps/entt/single_include"
          "${BALMUNG_DIR}/deps/entt/single_include")
if(NOT ENTT_INC)
    message(FATAL_ERROR "EnTT not found.  Put in deps/entt/ or sudo apt install libentt-dev")
endif()

# ── stb_image (header-only) ───────────────────────────────────────────────────
find_path(STB_INC stb/stb_image.h
    HINTS /usr/include /usr/local/include
          "${CMAKE_SOURCE_DIR}/deps" "${BALMUNG_DIR}/deps/stb")
if(NOT STB_INC)
    find_path(STB_INC stb_image.h
        HINTS "${CMAKE_SOURCE_DIR}/deps" "${BALMUNG_DIR}/deps/stb")
endif()

# ── Mono ──────────────────────────────────────────────────────────────────────
if(HV_ENABLE_MONO)
    find_path(MONO_INC mono/jit/jit.h
        HINTS /usr/include/mono-2.0 /usr/local/include/mono-2.0)
    find_library(MONO_LIB mono-2.0
        HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
    if(NOT MONO_INC OR NOT MONO_LIB)
        message(WARNING "Mono not found -- C# scripting disabled")
        set(HV_ENABLE_MONO OFF)
    endif()
endif()

# ── Balmung Engine ────────────────────────────────────────────────────────────
if(EXISTS "${BALMUNG_DIR}/include")
    set(BALMUNG_INC "${BALMUNG_DIR}/include")
    find_library(BALMUNG_LIB balmung HINTS "${BALMUNG_DIR}/lib" "${BALMUNG_DIR}/build/lib")
    message(STATUS "Balmung Engine: ${BALMUNG_DIR}")
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(HellVerdict
    src/main.cpp
    src/ecs/ecs_world.cpp
    src/world/world_map.cpp
    src/render/texture_cache.cpp
    src/render/vk_pipeline.cpp
    src/render/vk_frame_renderer.cpp
    src/render/sync/vk_sync.cpp
    src/render/font/sdf_font.cpp
    src/render/warmup/shader_warmup.cpp
    src/renderer/doom_renderer.cpp
    src/game/collision.cpp
    src/game/map_data.cpp
    src/game/player.cpp
    src/game/enemy.cpp
    src/game/hell_game.cpp
    src/scripting/cs_bridge.cpp
)

target_include_directories(HellVerdict PRIVATE
    src/ ${GLM_INC} ${ENTT_INC})
if(STB_INC)
    target_include_directories(HellVerdict PRIVATE ${STB_INC})
endif()
if(BALMUNG_INC)
    target_include_directories(HellVerdict PRIVATE ${BALMUNG_INC})
endif()

# ── Link ──────────────────────────────────────────────────────────────────────
target_link_libraries(HellVerdict PRIVATE SDL2::SDL2)
if(BALMUNG_LIB)
    target_link_libraries(HellVerdict PRIVATE ${BALMUNG_LIB})
endif()
if(HV_ENABLE_VULKAN)
    target_link_libraries(HellVerdict PRIVATE Vulkan::Vulkan)
    target_compile_definitions(HellVerdict PRIVATE HV_VULKAN=1)
endif()
if(HV_ENABLE_OPENGL AND OpenGL_FOUND)
    target_link_libraries(HellVerdict PRIVATE OpenGL::GL)
    if(TARGET glad) target_link_libraries(HellVerdict PRIVATE glad) endif()
    target_compile_definitions(HellVerdict PRIVATE HV_OPENGL=1)
endif()
if(HV_ENABLE_MONO)
    target_include_directories(HellVerdict PRIVATE ${MONO_INC})
    target_link_libraries(HellVerdict PRIVATE ${MONO_LIB})
    target_compile_definitions(HellVerdict PRIVATE HV_MONO=1)
endif()
if(HV_MOBILE)
    target_compile_definitions(HellVerdict PRIVATE HV_MOBILE=1)
    target_link_libraries(HellVerdict PRIVATE android log)
elseif(WIN32)
    target_link_libraries(HellVerdict PRIVATE ws2_32 user32 gdi32)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(HellVerdict PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
elseif(APPLE)
    target_link_libraries(HellVerdict PRIVATE
        "-framework Cocoa" "-framework IOKit" "-framework Metal" "-framework QuartzCore")
else()
    target_link_libraries(HellVerdict PRIVATE dl pthread)
endif()

# ── Shader compilation (GLSL -> SPIR-V) ──────────────────────────────────────
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VK_SDK_PATH}/bin")
if(GLSLC AND HV_ENABLE_VULKAN)
    set(SHADER_SRC "${CMAKE_SOURCE_DIR}/assets/shaders")
    set(SHADER_OUT "${CMAKE_BINARY_DIR}/bin/assets/shaders")
    file(MAKE_DIRECTORY ${SHADER_OUT})
    set(SPIRV_FILES)
    foreach(S world.vert world.frag bill.vert bill.frag
              hud.vert hud.frag present.vert present.frag
              sdf_text.vert sdf_text.frag)
        add_custom_command(
            OUTPUT  "${SHADER_OUT}/${S}.spv"
            COMMAND ${GLSLC} --target-env=vulkan1.3 -O
                    "${SHADER_SRC}/${S}" -o "${SHADER_OUT}/${S}.spv"
            DEPENDS "${SHADER_SRC}/${S}"
            COMMENT "glslc: ${S}")
        list(APPEND SPIRV_FILES "${SHADER_OUT}/${S}.spv")
    endforeach()
    add_custom_target(Shaders ALL DEPENDS ${SPIRV_FILES})
    add_dependencies(HellVerdict Shaders)
else()
    message(WARNING "glslc not found. Compile shaders manually before running.")
endif()

# ── Copy assets post-build ────────────────────────────────────────────────────
foreach(D maps fonts textures)
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/${D}")
        add_custom_command(TARGET HellVerdict POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets/${D}"
                "$<TARGET_FILE_DIR:HellVerdict>/assets/${D}")
    endif()
endforeach()
add_custom_command(TARGET HellVerdict POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scripts"
        "$<TARGET_FILE_DIR:HellVerdict>/scripts")

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== Hell Verdict v3 ===")
message(STATUS "  Build   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Vulkan  : ${HV_ENABLE_VULKAN}")
message(STATUS "  OpenGL  : ${HV_ENABLE_OPENGL}")
message(STATUS "  Mono/C# : ${HV_ENABLE_MONO}")
message(STATUS "  Mobile  : ${HV_MOBILE}")
message(STATUS "  Balmung : ${BALMUNG_DIR}")
message(STATUS "=======================")
