# -----------------------------------------------------------------------------
# carsim_core: renderer-independent simulation, map, traffic, collision, plate
# and persistence code. It depends only on CNA's math/core headers and on
# Sharp Runtime, so unit tests never need a graphics device.
# -----------------------------------------------------------------------------
add_library(carsim_core STATIC
    src/Audio/EngineSynth.cpp
    src/Audio/SoundSynth.cpp
    src/Collision/CollisionWorld.cpp
    src/Collision/Shapes.cpp
    src/Core/CommandLine.cpp
    src/Core/JsonReader.cpp
    src/Core/Noise.cpp
    src/Core/PiecewiseLinear.cpp
    src/Core/SaveData.cpp
    src/Core/Version.cpp
    src/Map/LaneGraph.cpp
    src/Map/MapData.cpp
    src/Map/MapDocument.cpp
    src/Map/MapWorld.cpp
    src/Map/ObjectPlacement.cpp
    src/Map/RoadNetwork.cpp
    src/Map/SpatialGrid.cpp
    src/Map/TerrainField.cpp
    src/Sim/CarStyle.cpp
    src/Sim/Clutch.cpp
    src/Sim/Electrics.cpp
    src/Sim/Engine.cpp
    src/Sim/EngineThermal.cpp
    src/Sim/FuelSystem.cpp
    src/Sim/Ground.cpp
    src/Sim/Odometer.cpp
    src/Sim/RigidBody.cpp
    src/Sim/Transmission.cpp
    src/Sim/Tyre.cpp
    src/Sim/Vehicle.cpp
    src/Sim/VehicleDefinition.cpp
    src/Traffic/PlateGenerator.cpp
    src/Traffic/TrafficSystem.cpp
)
target_include_directories(carsim_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(carsim_core PUBLIC CNA::Math CNA::CoreHeaders SharpRuntime::Text.Json)
carsim_apply_warnings(carsim_core)

# -----------------------------------------------------------------------------
# carsim_render: everything that touches GraphicsDevice, effects, textures,
# audio devices and input. Depends on the CNA umbrella (XNA 4.0 API surface).
# -----------------------------------------------------------------------------
add_library(carsim_render STATIC
    src/Audio/VehicleAudio.cpp
    src/Input/InputMapper.cpp
    src/Render/BitmapFont.cpp
    src/Render/BuildingGenerator.cpp
    src/Render/Camera.cpp
    src/Render/GpuMesh.cpp
    src/Render/GroundShadowBaker.cpp
    src/Render/Image.cpp
    src/Render/ImageText.cpp
    src/Render/InstrumentCluster.cpp
    src/Render/LightingRig.cpp
    src/Render/MeshData.cpp
    src/Render/MirrorView.cpp
    src/Render/PlateRenderer.cpp
    src/Render/CarTextures.cpp
    src/Render/CarWheels.cpp
    src/Render/ProceduralCar.cpp
    src/Render/ProceduralCockpit.cpp
    src/Render/ProceduralTextures.cpp
    src/Render/PropGenerator.cpp
    src/Render/RoadMeshBuilder.cpp
    src/Render/Screenshot.cpp
    src/Render/ShadowGeometry.cpp
    src/Render/SignGenerator.cpp
    src/Render/SkyRenderer.cpp
    src/Render/TestGround.cpp
    src/Render/TrafficRenderer.cpp
    src/Render/VegetationGenerator.cpp
    src/Render/VehicleRenderer.cpp
    src/Render/WorldRenderer.cpp
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
