find_path(
   ROCKSDB_INCLUDE_DIR
   NAMES rocksdb/db.h
   HINTS
      ${RocksDB_ROOT}
      $ENV{RocksDB_ROOT}
      ${ROCKSDB_ROOT}
      $ENV{ROCKSDB_ROOT}
      /opt/homebrew/opt/rocksdb
      /usr/local/opt/rocksdb
)

find_library(
   ROCKSDB_LIBRARY
   NAMES rocksdb
   HINTS
      ${RocksDB_ROOT}
      $ENV{RocksDB_ROOT}
      ${ROCKSDB_ROOT}
      $ENV{ROCKSDB_ROOT}
      /opt/homebrew/opt/rocksdb
      /usr/local/opt/rocksdb
   PATH_SUFFIXES lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
   RocksDB
   REQUIRED_VARS ROCKSDB_INCLUDE_DIR ROCKSDB_LIBRARY
)

if(RocksDB_FOUND AND NOT TARGET RocksDB::rocksdb)
   add_library(RocksDB::rocksdb UNKNOWN IMPORTED)
   set_target_properties(
      RocksDB::rocksdb
      PROPERTIES
         IMPORTED_LOCATION "${ROCKSDB_LIBRARY}"
         INTERFACE_INCLUDE_DIRECTORIES "${ROCKSDB_INCLUDE_DIR}"
   )
endif()
