# =============================================================================
# cna-car-simulator -- a Czech passenger-car driving simulator built on the
# XNA 4.0-compatible public API of the CNA framework ("next" branch) and the
# Sharp Runtime ("next" branch).
#
# Layout: application code lives under simulator/, command-line tools under
# tools/, unit tests under tests/. CNA and Sharp Runtime are sibling checkouts
# consumed with add_subdirectory(); they export no CMake package.
# =============================================================================
cmake_minimum_required(VERSION 3.23)

project(CnaCarSimulator
    VERSION 0.1.0
    DESCRIPTION "Czech passenger-car driving simulator on the CNA XNA 4.0-compatible API"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(CARSIM_BUILD_TESTS "Build the cna-car-simulator unit tests" ON)
option(CARSIM_BUILD_TOOLS "Build the cna-car-simulator command-line tools" ON)
option(CARSIM_WARNINGS_AS_ERRORS "Treat warnings in project code as errors" OFF)

set(CARSIM_CNA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../cna" CACHE PATH
    "Path to the CNA framework checkout (branch: next)")
set(CARSIM_SHARP_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../sharp-runtime" CACHE PATH
    "Path to the Sharp Runtime checkout (branch: next)")

foreach(_carsim_dep IN ITEMS CARSIM_CNA_ROOT CARSIM_SHARP_RUNTIME_ROOT)
    if(NOT EXISTS "${${_carsim_dep}}/CMakeLists.txt")
        message(FATAL_ERROR
            "cna-car-simulator: ${_carsim_dep} does not point at a checkout: '${${_carsim_dep}}'.\n"
            "Clone the dependency next to this repository (see README.md, 'Building') or pass "
            "-D${_carsim_dep}=/path/to/checkout.")
    endif()
endforeach()

# -----------------------------------------------------------------------------
# CNA configuration. Everything the simulator does not need is switched off to
# keep the framework build small. CNA_CNAEXT stays OFF: the project is bound to
# the XNA 4.0-compatible API only (see docs/api-boundary.md).
# -----------------------------------------------------------------------------
set(CNA_SHARP_RUNTIME_ROOT "${CARSIM_SHARP_RUNTIME_ROOT}" CACHE PATH "Sharp Runtime used by CNA" FORCE)
set(CNA_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
set(CNA_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(CNA_BUILD_C_API        OFF CACHE BOOL "" FORCE)
set(CNA_CNAEXT             OFF CACHE BOOL "" FORCE)
set(CNA_DEVICES            OFF CACHE BOOL "" FORCE)
set(CNA_ENABLE_NET         OFF CACHE BOOL "" FORCE)
set(CNA_ENABLE_VIDEO       OFF CACHE STRING "" FORCE)
set(CNA_ENABLE_DRACO       OFF CACHE BOOL "" FORCE)
set(EASYGL_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(EASYGL_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(SHARP_RUNTIME_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Keep CNA's persistent SDL build cache inside this project's build tree rather
# than inside the (possibly read-only) CNA checkout.
if(NOT DEFINED CNA_SDL_PREBUILT_ROOT)
    set(CNA_SDL_PREBUILT_ROOT
        "${CMAKE_BINARY_DIR}/cna-sdl-prebuilt-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_C_COMPILER_ID}"
        CACHE PATH "Persistent CNA SDL build cache")
endif()

# Sharp Runtime components: CNA's own closure plus Text.Json, which the
# simulator uses for vehicle definitions, maps and settings.
set(_carsim_sr_consumption "${CARSIM_CNA_ROOT}/cmake/SharpRuntimeConsumption.cmake")
if(NOT EXISTS "${_carsim_sr_consumption}")
    message(FATAL_ERROR "cna-car-simulator: CNA checkout lacks cmake/SharpRuntimeConsumption.cmake; "
                        "is CARSIM_CNA_ROOT on the 'next' branch?")
endif()
include("${_carsim_sr_consumption}")
set(_carsim_sr_components ${CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS} Text.Json)
list(REMOVE_DUPLICATES _carsim_sr_components)
set(SHARP_RUNTIME_COMPONENTS "${_carsim_sr_components}" CACHE STRING
    "Sharp Runtime components required by CNA and the simulator" FORCE)

add_subdirectory("${CARSIM_CNA_ROOT}" cna EXCLUDE_FROM_ALL)

if(CNA_CNAEXT)
    message(FATAL_ERROR "cna-car-simulator: CNA_CNAEXT must stay OFF; the simulator uses only the XNA 4.0 API.")
endif()
if(NOT TARGET CNA)
    message(FATAL_ERROR "cna-car-simulator: the CNA umbrella target was not created by the CNA checkout.")
endif()
if(NOT TARGET SharpRuntime::Text.Json)
    message(FATAL_ERROR "cna-car-simulator: SharpRuntime::Text.Json is missing; check SHARP_RUNTIME_COMPONENTS.")
endif()

message(STATUS "cna-car-simulator ${PROJECT_VERSION}: renderer ${CNA_GRAPHICS_RENDERER}, "
               "${CMAKE_BUILD_TYPE}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "cna-car-simulator: CNA at ${CARSIM_CNA_ROOT}")
message(STATUS "cna-car-simulator: Sharp Runtime at ${CARSIM_SHARP_RUNTIME_ROOT}")

include(CarSimWarnings)

add_subdirectory(simulator)

if(CARSIM_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(CARSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
