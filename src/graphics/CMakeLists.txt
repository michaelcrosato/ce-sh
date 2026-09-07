# lc_graphics: the Direct3D 12 layer. Windows only. No scene or game types here.
add_library(lc_graphics STATIC
    d3d12/acceleration_structure.cpp
    d3d12/acceleration_structure.h
    d3d12/compute_pipeline.cpp
    d3d12/compute_pipeline.h
    d3d12/d3d12_common.h
    d3d12/descriptor_heap.cpp
    d3d12/descriptor_heap.h
    d3d12/device.cpp
    d3d12/device.h
    d3d12/gpu_buffer.cpp
    d3d12/gpu_buffer.h
    d3d12/gpu_texture.cpp
    d3d12/gpu_texture.h
    d3d12/graphics_queue.cpp
    d3d12/graphics_queue.h
    d3d12/swap_chain.cpp
    d3d12/swap_chain.h
    d3d12/timestamp_queries.cpp
    d3d12/timestamp_queries.h
    d3d12/upload_arena.cpp
    d3d12/upload_arena.h
)
target_link_libraries(lc_graphics PUBLIC lc_core lc_platform d3d12 dxgi dxguid)
