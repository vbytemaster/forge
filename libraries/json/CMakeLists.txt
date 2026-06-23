add_library(forge_json STATIC json.cpp)
forge_target_modules(forge_json json)

target_include_directories(forge_json PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_json PUBLIC
      forge_core
      forge_config
      forge_schema
      forge_variant
   PRIVATE
      glaze::glaze
)
target_compile_definitions(
   forge_json
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
