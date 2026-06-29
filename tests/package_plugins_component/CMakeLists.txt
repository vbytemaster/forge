cmake_minimum_required(VERSION 3.31)

project(forge-package-plugins-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS plugins)

add_executable(forge_package_plugins_component main.cpp)
target_link_libraries(
   forge_package_plugins_component PRIVATE
      Forge::forge_plugins
      Forge::forge_plugins_p2p_node
      Forge::forge_plugins_crypto_secrets
      Forge::forge_plugins_log_otlp
)
if(TARGET Forge::forge_plugins_db_rocksdb)
   target_link_libraries(forge_package_plugins_component PRIVATE Forge::forge_plugins_db_rocksdb)
   target_compile_definitions(forge_package_plugins_component PRIVATE FORGE_PACKAGE_HAS_DB_ROCKSDB=1)
else()
   target_compile_definitions(forge_package_plugins_component PRIVATE FORGE_PACKAGE_HAS_DB_ROCKSDB=0)
endif()
