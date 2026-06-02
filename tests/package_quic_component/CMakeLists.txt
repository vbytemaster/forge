cmake_minimum_required(VERSION 3.31)

project(fcl-package-quic-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FCL CONFIG REQUIRED COMPONENTS quic)

add_executable(fcl_package_quic_component main.cpp)
target_link_libraries(fcl_package_quic_component PRIVATE FCL::fcl_quic)
