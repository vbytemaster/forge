add_library(fcl_env STATIC env.cpp)
fcl_target_modules(fcl_env env)

target_include_directories(fcl_env PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   fcl_env PUBLIC
      fcl_config
      fcl_schema
)
