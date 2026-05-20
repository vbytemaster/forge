add_library(
   fcl_api STATIC
   binding.cpp
   descriptor.cpp
   errors.cpp
   registry.cpp
)
fcl_target_modules(fcl_api api)

target_include_directories(fcl_api PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_api PUBLIC
      fcl_exception
      fcl_raw
      Boost::headers
)
