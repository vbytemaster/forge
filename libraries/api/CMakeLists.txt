add_library(
   fcl_api STATIC
   binding.cpp
   context.cpp
   dispatcher.cpp
   descriptor.cpp
   error_projection.cpp
   registry.cpp
)
fcl_target_modules(fcl_api api)

target_include_directories(fcl_api PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_api PUBLIC
      fcl_exceptions
      fcl_raw
      Boost::headers
)
