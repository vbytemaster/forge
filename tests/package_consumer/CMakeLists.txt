cmake_minimum_required(VERSION 3.31)

project(fcl-package-consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(FCL CONFIG REQUIRED COMPONENTS log exceptions raw crypto app)

add_executable(fcl_package_consumer main.cpp)
target_link_libraries(
   fcl_package_consumer PRIVATE
      FCL::fcl_log
      FCL::fcl_exceptions
      FCL::fcl_raw
      FCL::fcl_crypto
      FCL::fcl_app
)
