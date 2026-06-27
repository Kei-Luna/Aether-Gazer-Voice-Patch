cmake_minimum_required(VERSION 3.20)

project(AetherGazerVoicePatch LANGUAGES C CXX)

add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC third_party/minhook/include)

add_library(AetherGazerVoicePatch SHARED
    src/dllmain.cpp
    src/common.cpp
    src/state.cpp
    src/il2cpp.cpp
    src/voice_redirect.cpp
    src/patch_internal.hpp
)

target_compile_features(AetherGazerVoicePatch PRIVATE cxx_std_20)
target_compile_definitions(AetherGazerVoicePatch PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(AetherGazerVoicePatch PRIVATE minhook)

if(MSVC)
    target_compile_options(AetherGazerVoicePatch PRIVATE /utf-8 /W4 /permissive-)
endif()

set_target_properties(AetherGazerVoicePatch PROPERTIES
    OUTPUT_NAME "AetherGazer-VoicePatch"
)
