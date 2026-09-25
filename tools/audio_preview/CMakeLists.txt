# Deterministic, device-free renders of the actual project audio mixer for listening review.
add_executable(carsim-audiopreview main.cpp)
target_link_libraries(carsim-audiopreview PRIVATE carsim_render)
carsim_apply_warnings(carsim-audiopreview)
