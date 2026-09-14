# Unit tests use GoogleTest. CNA vendors a googletest submodule; it is reused
# here so no network access is needed at configure time.
if(NOT TARGET GTest::gtest)
    set(_carsim_gtest_dir "${CARSIM_CNA_ROOT}/vendor/googletest")
    if(NOT EXISTS "${_carsim_gtest_dir}/CMakeLists.txt")
        message(FATAL_ERROR
            "cna-car-simulator: googletest not found at ${_carsim_gtest_dir}. Run\n"
            "  git -C ${CARSIM_CNA_ROOT} submodule update --init vendor/googletest\n"
            "or configure with -DCARSIM_BUILD_TESTS=OFF.")
    endif()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory("${_carsim_gtest_dir}" googletest EXCLUDE_FROM_ALL)
endif()

add_executable(carsim_tests
    CommandLineTests.cpp
    Core/PiecewiseLinearTests.cpp
    Core/SaveDataTests.cpp
    Sim/ClutchTests.cpp
    Sim/ElectricsTests.cpp
    Sim/EngineTests.cpp
    Sim/FuelThermalTests.cpp
    Sim/FuelCycleTests.cpp
    Sim/RigidBodyTests.cpp
    Sim/TransmissionTests.cpp
    Sim/TyreGroundTests.cpp
    Sim/VehicleDefinitionTests.cpp
    Sim/VehicleDriveTests.cpp
    Render/MeshDataTests.cpp
    Render/ProceduralCarTests.cpp
    Render/ShadowGeometryTests.cpp
    Map/MapDocumentTests.cpp
    Map/RoadNetworkTests.cpp
    Map/LaneGraphTests.cpp
    Map/TerrainAndGroundTests.cpp
    Map/SampleMapTests.cpp
    Collision/CollisionTests.cpp
    Traffic/PlateTests.cpp
    Traffic/TrafficTests.cpp
    Traffic/TrafficSoakTests.cpp
    Audio/EngineSynthTests.cpp
    Render/ImageTests.cpp
)
target_link_libraries(carsim_tests PRIVATE carsim_render carsim_core GTest::gtest GTest::gtest_main)
target_compile_definitions(carsim_tests PRIVATE CARSIM_TEST_CONTENT_DIR="${CMAKE_SOURCE_DIR}/content")
carsim_apply_warnings(carsim_tests)
add_test(NAME carsim_unit_tests COMMAND carsim_tests)
set_tests_properties(carsim_unit_tests PROPERTIES TIMEOUT 300 LABELS "unit")

# Static guard: project code may use only the XNA 4.0-compatible API surface
# of CNA plus Sharp Runtime. See docs/api-boundary.md.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME xna_only_api_check
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/check_xna_only.py"
                --root "${CMAKE_SOURCE_DIR}")
    set_tests_properties(xna_only_api_check PROPERTIES TIMEOUT 120 LABELS "static")
endif()

# Smoke test: run the simulator for a few frames without a display requirement
# beyond what the selected renderer needs. Labeled so headless CI can pick.
if(UNIX AND NOT APPLE)
    # scripts/run_headless.sh starts Xvfb when no DISPLAY is set and selects the dummy audio driver.
    add_test(NAME simulator_smoke
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/run_headless.sh $<TARGET_FILE:cna-car-simulator> --frames 3 --width 320 --height 200)
else()
    add_test(NAME simulator_smoke
        COMMAND cna-car-simulator --frames 3 --width 320 --height 200)
endif()
set_tests_properties(simulator_smoke PROPERTIES TIMEOUT 120 LABELS "smoke;display")

# Map validation over the shipped content (referential integrity, connectivity, grades).
add_test(NAME map_validate_lipova
    COMMAND carsim-mapvalidate --content "${CMAKE_SOURCE_DIR}/content" --map lipova --quiet)
set_tests_properties(map_validate_lipova PROPERTIES TIMEOUT 120 LABELS "content")

# Asset provenance: every external file listed with licence and matching SHA-256.
add_test(NAME asset_manifest_check
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/scripts/check_assets.py" --root "${CMAKE_SOURCE_DIR}")
set_tests_properties(asset_manifest_check PROPERTIES LABELS "static")
