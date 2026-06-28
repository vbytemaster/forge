add_library(
   forge_rocksdb STATIC
   store.cpp
   types.cpp
)
forge_target_modules(forge_rocksdb rocksdb)

target_include_directories(forge_rocksdb PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_rocksdb PUBLIC
      forge_exceptions
      forge_schema
   PRIVATE
      RocksDB::rocksdb
)

target_compile_definitions(forge_rocksdb PUBLIC FORGE_HAVE_ROCKSDB=1)
