file(GLOB FORGE_MULTIFORMATS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(forge_multiformats STATIC ${FORGE_MULTIFORMATS_SOURCES})
forge_target_modules(forge_multiformats multiformats)

target_include_directories(forge_multiformats PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_multiformats PUBLIC
      forge_exceptions
      forge_crypto
)
