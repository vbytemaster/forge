add_library(
   forge_api STATIC
   binding.cpp
   context.cpp
   dispatcher.cpp
   descriptor.cpp
   error_projection.cpp
   registry.cpp
)
forge_target_modules(forge_api api)

target_include_directories(forge_api PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_api PUBLIC
      forge_exceptions
      forge_raw
      Boost::headers
)
