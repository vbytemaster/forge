cmake_minimum_required(VERSION 3.31)

project(fcl-package-http-api-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FCL CONFIG REQUIRED COMPONENTS http_api)

add_executable(fcl_package_http_api_component main.cpp)
target_link_libraries(fcl_package_http_api_component PRIVATE FCL::fcl_http_api)
