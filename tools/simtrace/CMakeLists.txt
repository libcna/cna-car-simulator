# Headless scripted drives that print a time trace of the vehicle state. Used to inspect
# and tune the simulation without the renderer.
add_executable(carsim-simtrace main.cpp)
target_link_libraries(carsim-simtrace PRIVATE carsim_core)
carsim_apply_warnings(carsim-simtrace)
