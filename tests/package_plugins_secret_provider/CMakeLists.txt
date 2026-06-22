cmake_minimum_required(VERSION 3.31)

project(fcl-package-plugins-secret-provider LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FCL CONFIG REQUIRED COMPONENTS plugins_secret_provider)

add_executable(fcl_package_plugins_secret_provider main.cpp)
target_link_libraries(fcl_package_plugins_secret_provider PRIVATE FCL::fcl_plugins_secret_provider)
