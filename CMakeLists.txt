cmake_minimum_required(VERSION 3.20)
project(HonkHonk LANGUAGES CXX)

set(FH2MOD_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../FH2-Mod-Platform-v2.8.0/include"
    CACHE PATH "Directory containing the FH2 Mod Platform headers")
if(NOT EXISTS "${FH2MOD_INCLUDE_DIR}/fh2mod.h")
    message(FATAL_ERROR
        "FH2-Mod-Platform-v2.8.0/include/fh2mod.h was not found beside this project.")
endif()

add_library(HonkHonkAudio STATIC HonkHonkAudio.cpp)
target_compile_features(HonkHonkAudio PUBLIC cxx_std_17)
target_compile_definitions(HonkHonkAudio PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(HonkHonkAudio PUBLIC winmm)

add_library(HonkHonk SHARED HonkHonk.cpp FmodHorn.cpp)
target_compile_features(HonkHonk PRIVATE cxx_std_17)
target_include_directories(HonkHonk PRIVATE "${FH2MOD_INCLUDE_DIR}")
target_compile_definitions(HonkHonk PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(HonkHonk PRIVATE HonkHonkAudio)
set_target_properties(HonkHonk PROPERTIES
    PREFIX ""
    OUTPUT_NAME "HonkHonk"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")

add_executable(HonkHonkAudioValidation tests/audio_validation.cpp)
target_compile_features(HonkHonkAudioValidation PRIVATE cxx_std_17)
target_compile_definitions(HonkHonkAudioValidation PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(HonkHonkAudioValidation PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(HonkHonkAudioValidation PRIVATE HonkHonkAudio)

add_executable(HonkHonkGameplayValidation tests/gameplay_validation.cpp)
target_compile_features(HonkHonkGameplayValidation PRIVATE cxx_std_17)
target_compile_definitions(HonkHonkGameplayValidation PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(HonkHonkGameplayValidation PRIVATE "${FH2MOD_INCLUDE_DIR}")

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(HonkHonk PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_CURRENT_SOURCE_DIR}/dist"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_CURRENT_SOURCE_DIR}/dist")
endforeach()
