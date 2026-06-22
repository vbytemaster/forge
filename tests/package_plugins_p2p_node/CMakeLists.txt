cmake_minimum_required(VERSION 3.31)

project(fcl-package-plugins-p2p-node LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FCL CONFIG REQUIRED COMPONENTS plugins_p2p_node)

add_executable(fcl_package_plugins_p2p_node main.cpp)
target_link_libraries(fcl_package_plugins_p2p_node PRIVATE FCL::fcl_plugins_p2p_node)
