# lc_scene: portable scene data (meshes, instances, camera, generated scenes). No D3D12 types.
add_library(lc_scene STATIC
    builtin_scenes.cpp
    builtin_scenes.h
    camera.cpp
    camera.h
    mesh.cpp
    mesh.h
    primitives.cpp
    primitives.h
    scene.cpp
    scene.h
)
target_link_libraries(lc_scene PUBLIC lc_core)
