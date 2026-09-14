add_executable(carsim-mapvalidate main.cpp)
target_link_libraries(carsim-mapvalidate PRIVATE carsim_core)
carsim_apply_warnings(carsim-mapvalidate)
