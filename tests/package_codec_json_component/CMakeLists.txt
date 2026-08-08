cmake_minimum_required(VERSION 3.31)

project(forge-package-codec-json-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS codec_json)
if(DEFINED FORGE_EXPECTED_PREFIX_PATH AND NOT CMAKE_PREFIX_PATH STREQUAL FORGE_EXPECTED_PREFIX_PATH)
   message(FATAL_ERROR "JSON package consumer changed dependency prefixes from ${FORGE_EXPECTED_PREFIX_PATH} to ${CMAKE_PREFIX_PATH}")
endif()
if(DEFINED FORGE_EXPECTED_PACKAGE_DIR AND NOT Forge_DIR STREQUAL FORGE_EXPECTED_PACKAGE_DIR)
   message(FATAL_ERROR "JSON package consumer resolved Forge from ${Forge_DIR}, expected ${FORGE_EXPECTED_PACKAGE_DIR}")
endif()
if(DEFINED FORGE_EXPECTED_GLAZE_DIR AND NOT glaze_DIR STREQUAL FORGE_EXPECTED_GLAZE_DIR)
   message(FATAL_ERROR "JSON package consumer resolved Glaze from ${glaze_DIR}, expected ${FORGE_EXPECTED_GLAZE_DIR}")
endif()

add_executable(forge_package_codec_json_component main.cpp)
target_sources(
   forge_package_codec_json_component
   PRIVATE
      FILE_SET CXX_MODULES
      FILES exact_types.cppm
)
target_link_libraries(forge_package_codec_json_component PRIVATE Forge::forge_codec_json)
