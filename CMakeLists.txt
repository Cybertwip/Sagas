cmake_minimum_required(VERSION 3.25)
project(Sagas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SAGAS_BUILD_TESTS "Build the compact engine tests" ON)
set(SAGAS_REMIX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../remix" CACHE PATH
    "Path to the Smash Remix source tree")
set(SAGAS_ASSET_ROOT "${CMAKE_CURRENT_BINARY_DIR}/assets" CACHE PATH
    "Generated, external asset bundle")

find_package(SDL3 CONFIG REQUIRED)
find_package(PNG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(sagas_asset_unpack tools/unpack_assets.cpp)
target_compile_options(sagas_asset_unpack PRIVATE -Wall -Wextra -Wpedantic)

set(SAGAS_ASSET_STAMP "${SAGAS_ASSET_ROOT}/.complete")
add_custom_command(
    OUTPUT "${SAGAS_ASSET_STAMP}"
    COMMAND sagas_asset_unpack
            --remix-root "${SAGAS_REMIX_ROOT}"
            --output "${SAGAS_ASSET_ROOT}"
    DEPENDS sagas_asset_unpack
            "${SAGAS_REMIX_ROOT}/game/assets/us/relocData.csv"
            "${SAGAS_REMIX_ROOT}/game/assets/us/relocData.bin"
            "${SAGAS_REMIX_ROOT}/game/tools/relocFileDescriptions.us.txt"
            "${SAGAS_REMIX_ROOT}/game/include/reloc_data.us.h"
    COMMENT "Unpacking Remix relocation, texture, and audio assets"
    VERBATIM)
add_custom_target(sagas_assets ALL DEPENDS "${SAGAS_ASSET_STAMP}")

set(SAGAS_OPENING_CSEQ "${CMAKE_CURRENT_BINARY_DIR}/generated/seq_33_Opening.cseq")
set(SAGAS_OPENING_MUSIC "${SAGAS_ASSET_ROOT}/audio/opening.sgm")
set(SAGAS_OPENING_PCM "${SAGAS_ASSET_ROOT}/audio/opening.sgpcm")
add_custom_command(
    OUTPUT "${SAGAS_OPENING_CSEQ}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${Python3_EXECUTABLE}"
            "${SAGAS_REMIX_ROOT}/game/tools/assemble_cseq.py"
            "${SAGAS_REMIX_ROOT}/game/build/us/src/audio/S1_music/seq_33_Opening.cseq.s"
            -o "${SAGAS_OPENING_CSEQ}"
    DEPENDS "${SAGAS_REMIX_ROOT}/game/build/us/src/audio/S1_music/seq_33_Opening.cseq.s"
            "${SAGAS_REMIX_ROOT}/game/tools/assemble_cseq.py"
    VERBATIM)
add_custom_command(
    OUTPUT "${SAGAS_OPENING_MUSIC}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_music.py"
            --generator "${SAGAS_REMIX_ROOT}/tools/generate_zako_music.py"
            --parser "${SAGAS_REMIX_ROOT}/game/tools/cseq_to_mid.py"
            --bank "${SAGAS_REMIX_ROOT}/game/build/us/src/audio/B1_sounds1.ctl.json"
            --sequence "${SAGAS_OPENING_CSEQ}"
            --output "${SAGAS_OPENING_MUSIC}"
    DEPENDS "${SAGAS_OPENING_CSEQ}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_music.py"
            "${SAGAS_REMIX_ROOT}/tools/generate_zako_music.py"
            "${SAGAS_REMIX_ROOT}/game/tools/cseq_to_mid.py"
            "${SAGAS_REMIX_ROOT}/game/build/us/src/audio/B1_sounds1.ctl.json"
            "${SAGAS_ASSET_STAMP}"
    VERBATIM)
add_custom_command(
    OUTPUT "${SAGAS_OPENING_PCM}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_music.py"
            --input "${SAGAS_OPENING_MUSIC}"
            --wave-root "${SAGAS_ASSET_ROOT}/audio/B1_sounds1"
            --output "${SAGAS_OPENING_PCM}"
            --gain 0.72
    DEPENDS "${SAGAS_OPENING_MUSIC}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bake_music.py"
            "${SAGAS_ASSET_STAMP}"
    COMMENT "Baking opening music for zero-stall playback"
    VERBATIM)
add_custom_target(sagas_music_assets ALL DEPENDS "${SAGAS_OPENING_PCM}")

set(SAGAS_OPENING_SCENE "${SAGAS_ASSET_ROOT}/scenes/opening.sgscene")
add_custom_command(
    OUTPUT "${SAGAS_OPENING_SCENE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_scene.py"
            --input "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/opening.scene.tsv"
            --output "${SAGAS_OPENING_SCENE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_scene.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/opening.scene.tsv"
            "${SAGAS_ASSET_STAMP}"
    COMMENT "Baking opening scene resource manifest"
    VERBATIM)
add_custom_target(sagas_scene_assets ALL DEPENDS "${SAGAS_OPENING_SCENE}")

add_library(sagas_engine
    src/Application.cpp
    src/Audio.cpp
    src/Core.cpp
    src/Lighting.cpp
    src/n64/Animation.cpp
    src/n64/Archive.cpp
    src/n64/DisplayList.cpp
    src/n64/SceneGraph.cpp
    src/Render.cpp
    src/Scene.cpp
    src/Scene3D.cpp
    src/SceneResources.cpp)
target_sources(sagas_engine PRIVATE
    src/scenes/StartupScene.cpp
    src/scenes/OpeningScene.cpp
    src/scenes/TitleScene.cpp)
target_include_directories(sagas_engine PUBLIC include)
target_link_libraries(sagas_engine PUBLIC SDL3::SDL3 PNG::PNG OpenGL::GL)
target_compile_definitions(sagas_engine PUBLIC
    SAGAS_DEFAULT_ASSET_ROOT="${SAGAS_ASSET_ROOT}")
target_compile_options(sagas_engine PRIVATE -Wall -Wextra -Wpedantic)

add_executable(sagas app/main.cpp)
target_link_libraries(sagas PRIVATE sagas_engine)
add_dependencies(sagas_music_assets sagas_assets)
add_dependencies(sagas_scene_assets sagas_assets)
add_dependencies(sagas sagas_music_assets)
add_dependencies(sagas sagas_scene_assets)
add_dependencies(sagas sagas_asset_unpack)


if(SAGAS_BUILD_TESTS)
    enable_testing()
    add_executable(sagas_tests tests/core_tests.cpp)
    target_link_libraries(sagas_tests PRIVATE sagas_engine)
    add_dependencies(sagas_tests sagas_assets)
    add_test(NAME sagas_core COMMAND sagas_tests)
endif()
