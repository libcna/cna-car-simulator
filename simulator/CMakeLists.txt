# -----------------------------------------------------------------------------
# carsim_core: renderer-independent simulation, map, traffic, collision, plate
# and persistence code. It depends only on CNA's math/core headers and on
# Sharp Runtime, so unit tests never need a graphics device.
# -----------------------------------------------------------------------------
add_library(carsim_core STATIC
    src/Core/CommandLine.cpp
    src/Core/Version.cpp
)
target_include_directories(carsim_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(carsim_core PUBLIC CNA::Math CNA::CoreHeaders SharpRuntime::Text.Json)
carsim_apply_warnings(carsim_core)

# -----------------------------------------------------------------------------
# carsim_render: everything that touches GraphicsDevice, effects, textures,
# audio devices and input. Depends on the CNA umbrella (XNA 4.0 API surface).
# -----------------------------------------------------------------------------
add_library(carsim_render STATIC
    src/Render/Screenshot.cpp
)
target_include_directories(carsim_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(carsim_render PUBLIC carsim_core CNA)
carsim_apply_warnings(carsim_render)

# -----------------------------------------------------------------------------
# The simulator executable.
# -----------------------------------------------------------------------------
add_executable(cna-car-simulator
    src/App/Program.cpp
    src/App/SimulatorGame.cpp
)
target_link_libraries(cna-car-simulator PRIVATE carsim_render carsim_core CNA)
if(TARGET SDL3::SDL3main)
    target_link_libraries(cna-car-simulator PRIVATE SDL3::SDL3main)
endif()
carsim_apply_warnings(cna-car-simulator)

# The content root is copied next to the executable so relative paths resolve
# at runtime; the source location is also compiled in as a development fallback
# (used only when no content directory is found next to the binary).
set(CARSIM_CONTENT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/content")
target_compile_definitions(cna-car-simulator PRIVATE
    CARSIM_SOURCE_CONTENT_DIR="${CARSIM_CONTENT_SOURCE_DIR}")
if(EXISTS "${CARSIM_CONTENT_SOURCE_DIR}")
    add_custom_command(TARGET cna-car-simulator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CARSIM_CONTENT_SOURCE_DIR}" "$<TARGET_FILE_DIR:cna-car-simulator>/content"
        COMMENT "Copying simulator content next to the executable")
endif()

if(WIN32 AND COMMAND cna_copy_sdl_runtime)
    cna_copy_sdl_runtime(cna-car-simulator)
endif()
