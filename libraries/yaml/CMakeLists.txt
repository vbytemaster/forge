add_library(forge_yaml STATIC yaml.cpp)
forge_target_modules(forge_yaml yaml)

target_include_directories(forge_yaml PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_yaml PUBLIC
      forge_config
      forge_schema
      forge_variant
   PRIVATE
      glaze::glaze
)
