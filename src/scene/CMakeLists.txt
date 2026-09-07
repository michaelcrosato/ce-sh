# lc_scene: portable scene data (meshes, materials, instances, emitters, rooms, camera, generated scenes). No D3D12 types.
add_library(lc_scene STATIC
    builtin_scenes.cpp
    builtin_scenes.h
    camera.cpp
    camera.h
    emitters.cpp
    emitters.h
    expectations.h
    material.cpp
    material.h
    mesh.cpp
    mesh.h
    primitives.cpp
    primitives.h
    rooms.cpp
    rooms.h
    scene.cpp
    scene.h
    scene_file.cpp
    scene_file.h
    two_room_level.cpp
    two_room_level.h
)
target_link_libraries(lc_scene PUBLIC lc_core)
