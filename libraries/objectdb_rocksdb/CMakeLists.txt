add_library(
   forge_objectdb_rocksdb STATIC
   rocksdb.cpp
)
forge_target_modules_at(forge_objectdb_rocksdb objectdb)

target_include_directories(forge_objectdb_rocksdb PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_objectdb_rocksdb PUBLIC
      forge_objectdb
      forge_rocksdb
)
