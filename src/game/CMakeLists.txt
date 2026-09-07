# lc_game: simulation, input, replay, and (from Task 4) the world. No D3D12 types.
add_library(lc_game STATIC
    input.h
    replay.cpp
    replay.h
    simulation.cpp
    simulation.h
)
target_link_libraries(lc_game PUBLIC lc_scene lc_core)
