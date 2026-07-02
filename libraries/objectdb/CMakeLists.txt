add_library(
   forge_objectdb STATIC
   snapshot.cpp
   store.cpp
   store_impl.cpp
   transaction.cpp
   write_gate.cpp
)
forge_target_modules(forge_objectdb objectdb)

target_include_directories(forge_objectdb PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_objectdb PUBLIC
      forge_exceptions
      forge_ids
      forge_raw
)
