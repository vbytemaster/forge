add_library(fcl_schema STATIC schema.cpp)
fcl_target_modules(fcl_schema schema)

target_include_directories(fcl_schema PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_schema PUBLIC
      fcl_reflect
      Boost::headers
)
