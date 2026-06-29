add_library(
   forge_plugins_db_rocksdb STATIC
   plugin.cpp
   config.cpp
   plugin_impl.cpp
   transaction_impl.cpp
   api.cpp
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
      forge_rocksdb
      forge_exceptions
      Boost::asio
)

target_compile_definitions(forge_plugins_db_rocksdb PUBLIC FORGE_HAVE_ROCKSDB=1)
