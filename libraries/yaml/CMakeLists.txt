add_library(fcl_yaml STATIC yaml.cpp)
fcl_target_modules(fcl_yaml yaml)

target_include_directories(fcl_yaml PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_yaml PUBLIC
      fcl_config
      fcl_schema
      fcl_variant
   PRIVATE
      glaze::glaze
)
