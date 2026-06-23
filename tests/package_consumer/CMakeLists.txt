cmake_minimum_required(VERSION 3.31)

project(forge-package-consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS log exceptions raw crypto app)

add_executable(forge_package_consumer main.cpp)
target_link_libraries(
   forge_package_consumer PRIVATE
      Forge::forge_log
      Forge::forge_exceptions
      Forge::forge_raw
      Forge::forge_crypto
      Forge::forge_app
)
