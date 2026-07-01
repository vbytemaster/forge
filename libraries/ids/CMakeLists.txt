add_library(forge_ids STATIC)
forge_target_modules(forge_ids ids)

target_include_directories(forge_ids PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_ids PUBLIC
      forge_raw
      forge_variant
)
