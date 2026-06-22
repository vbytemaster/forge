cmake_minimum_required(VERSION 3.31)

project(fcl-package-plugins-http-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FCL CONFIG REQUIRED COMPONENTS plugins_http_server)

add_executable(fcl_package_plugins_http_server main.cpp)
target_link_libraries(fcl_package_plugins_http_server PRIVATE FCL::fcl_plugins_http_server)
