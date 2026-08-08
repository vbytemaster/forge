cmake_minimum_required(VERSION 3.31)

project(forge-package-db-mdbx-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS db_mdbx)
if(DEFINED FORGE_EXPECTED_PACKAGE_DIR AND NOT Forge_DIR STREQUAL FORGE_EXPECTED_PACKAGE_DIR)
   message(FATAL_ERROR "MDBX package consumer resolved Forge from ${Forge_DIR}, expected ${FORGE_EXPECTED_PACKAGE_DIR}")
endif()

add_executable(forge_package_db_mdbx_component main.cpp)
target_link_libraries(
   forge_package_db_mdbx_component
   PRIVATE Forge::forge_db_mdbx
)
