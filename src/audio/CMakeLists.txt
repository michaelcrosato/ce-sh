# lc_miniaudio: miniaudio (pinned submodule, docs/DEPENDENCIES.md) compiled once as C from its own
# miniaudio.c. Only the WASAPI backend; no file decoding, encoding, generators, or resource manager:
# every clip is generated in code and handed over as a PCM buffer.
set(_ma "${CMAKE_SOURCE_DIR}/external/miniaudio")
if(NOT EXISTS "${_ma}/miniaudio.c")
    message(FATAL_ERROR "external/miniaudio is empty; run git submodule update --init --recursive")
endif()
add_library(lc_miniaudio STATIC "${_ma}/miniaudio.c" "${_ma}/miniaudio.h")
target_include_directories(lc_miniaudio PUBLIC "${_ma}")
# PUBLIC: these change miniaudio's structure layouts, so every includer must agree with the library.
target_compile_definitions(lc_miniaudio PUBLIC
    MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER
    MA_ENABLE_ONLY_SPECIFIC_BACKENDS MA_ENABLE_WASAPI)
if(MSVC)
    target_compile_options(lc_miniaudio PRIVATE /W3)
endif()
set_target_properties(lc_miniaudio PROPERTIES FOLDER "external")

# lc_audio: generated clips, the mixing rules (attenuation, pan, occlusion, state-following), and
# the device layer on miniaudio. The rules and clips have no device dependency (CPU-tested).
add_library(lc_audio STATIC
    audio_system.cpp
    audio_system.h
    clips.cpp
    clips.h
    director.cpp
    director.h
)
target_link_libraries(lc_audio PUBLIC lc_core PRIVATE lc_miniaudio)
