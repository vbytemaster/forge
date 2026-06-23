add_library(
   forge_raw STATIC
   datastream.cpp
   raw.cpp
   varint.cpp
)
forge_target_modules(forge_raw raw)
target_include_directories(forge_raw PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_raw PUBLIC
      forge_core
      forge_exceptions
      forge_reflect
      forge_variant
      Boost::headers
      Boost::multiprecision
)
