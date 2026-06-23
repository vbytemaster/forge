add_library(forge_reflect STATIC reflect.cpp)
forge_target_modules(forge_reflect reflect)
target_include_directories(forge_reflect PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_link_libraries(
   forge_reflect PUBLIC
      forge_core
      Boost::headers
)
