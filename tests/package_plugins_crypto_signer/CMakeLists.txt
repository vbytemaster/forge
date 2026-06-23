cmake_minimum_required(VERSION 3.31)

project(forge-package-plugins-crypto-signer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS plugins_crypto_signer)

add_executable(forge_package_plugins_crypto_signer main.cpp)
target_link_libraries(forge_package_plugins_crypto_signer PRIVATE Forge::forge_plugins_crypto_signer)
