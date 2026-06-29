cmake_minimum_required(VERSION 3.31)

project(forge-package-plugins-db-rocksdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS plugins_db_rocksdb)

add_executable(forge_package_plugins_db_rocksdb main.cpp)
target_link_libraries(forge_package_plugins_db_rocksdb PRIVATE Forge::forge_plugins_db_rocksdb)
