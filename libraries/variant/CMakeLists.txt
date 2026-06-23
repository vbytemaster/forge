add_library(
   forge_variant STATIC
   variant_chrono.cpp
   variant.cpp
   variant_format.cpp
   variant_object.cpp
)
forge_target_modules(forge_variant variant)

target_include_directories(forge_variant PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_variant PUBLIC
      forge_core
      forge_exceptions
      forge_reflect
      Boost::headers
      Boost::multi_index
      Boost::multiprecision
)
target_compile_definitions(
   forge_variant
   PUBLIC OPENSSL_API_COMPAT=0x30000000L
          OPENSSL_NO_DEPRECATED
)
