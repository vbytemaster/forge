add_library(
   fcl_raw STATIC
   datastream.cpp
   raw.cpp
   varint.cpp
)
fcl_target_modules(fcl_raw raw)
target_include_directories(fcl_raw PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   fcl_raw PUBLIC
      fcl_core
      fcl_exceptions
      fcl_reflect
      fcl_variant
      Boost::headers
      Boost::multiprecision
)
