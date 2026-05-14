cmake_minimum_required(VERSION 3.31)

project(fcl-package-env-lightweight LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FCL CONFIG REQUIRED COMPONENTS env)

add_executable(fcl_package_env_lightweight main.cpp)
target_link_libraries(fcl_package_env_lightweight PRIVATE FCL::fcl_env)
