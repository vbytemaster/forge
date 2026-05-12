add_library(fcl_json STATIC json.cpp)
fcl_target_modules(fcl_json json)

target_include_directories(fcl_json PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   fcl_json PUBLIC
      fcl_core
      fcl_config
      fcl_schema
      fcl_variant
   PRIVATE
      glaze::glaze
)
target_compile_definitions(
   fcl_json
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
