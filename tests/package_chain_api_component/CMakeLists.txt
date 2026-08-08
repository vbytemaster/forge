cmake_minimum_required(VERSION 3.31)

project(forge-package-chain-api-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(
   Forge CONFIG REQUIRED
   COMPONENTS
      api_core
      api_http
      app
      asio
      chain_api
      chain_protocol
      config_core
      crypto_digest
      net_http
      net_p2p
      plugins_p2p_node
      plugins_p2p_resolver
)

add_executable(forge_package_chain_api_component main.cpp)
target_link_libraries(
   forge_package_chain_api_component
   PRIVATE
      Forge::forge_api_core
      Forge::forge_api_http
      Forge::forge_app
      Forge::forge_asio
      Forge::forge_chain_api
      Forge::forge_chain_protocol
      Forge::forge_config_core
      Forge::forge_crypto_digest
      Forge::forge_net_http
      Forge::forge_net_p2p
      Forge::forge_plugins_p2p_node
      Forge::forge_plugins_p2p_resolver
)
