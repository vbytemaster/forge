add_library(
   fcl_variant STATIC
   variant_chrono.cpp
   variant.cpp
   variant_format.cpp
   variant_object.cpp
)
fcl_target_modules(fcl_variant variant)

target_include_directories(fcl_variant PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   fcl_variant PUBLIC
      fcl_core
      fcl_exceptions
      fcl_reflect
      Boost::headers
      Boost::multi_index
      Boost::multiprecision
)
target_compile_definitions(
   fcl_variant
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
