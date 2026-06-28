if(NOT TARGET RocksDB::rocksdb)
   find_package(RocksDB CONFIG REQUIRED)
endif()

if(TARGET RocksDB::rocksdb-shared)
   set(FORGE_ROCKSDB_TARGET RocksDB::rocksdb-shared)
elseif(TARGET RocksDB::rocksdb)
   set(FORGE_ROCKSDB_TARGET RocksDB::rocksdb)
elseif(TARGET RocksDB::RocksDB)
   set(FORGE_ROCKSDB_TARGET RocksDB::RocksDB)
elseif(TARGET rocksdb)
   set(FORGE_ROCKSDB_TARGET rocksdb)
else()
   message(FATAL_ERROR "RocksDB package was found, but no supported CMake target was exported")
endif()

add_library(
   forge_plugins_db_rocksdb STATIC
      plugin.cpp
      config.cpp
      native_store_impl.cpp
      plugin_impl.cpp
      transaction_impl.cpp
      api.cpp
      types.cpp
)
forge_target_modules_at(forge_plugins_db_rocksdb plugins/db/rocksdb)

target_include_directories(
   forge_plugins_db_rocksdb
   PUBLIC
      ${FORGE_PUBLIC_INCLUDE_DIRS}
      "$<BUILD_INTERFACE:${FORGE_PLUGINS_DB_ROCKSDB_INCLUDE_DIR}>"
   PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
   forge_plugins_db_rocksdb PUBLIC
      forge_api
      forge_app
      forge_asio
      forge_config
      forge_schema
      forge_exceptions
      Boost::asio
   PRIVATE
      "${FORGE_ROCKSDB_TARGET}"
)

target_compile_definitions(forge_plugins_db_rocksdb PUBLIC FORGE_HAVE_ROCKSDB=1)
