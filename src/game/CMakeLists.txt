# lc_game: simulation, input, replay, and the two-room world. No D3D12 types.
add_library(lc_game STATIC
    collision.cpp
    collision.h
    input.h
    replay.cpp
    replay.h
    simulation.cpp
    simulation.h
    world.cpp
    world.h
)
target_link_libraries(lc_game PUBLIC lc_scene lc_core)
