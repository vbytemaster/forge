add_library(
   forge_db_authenticated STATIC
   codec.cpp
   hash.cpp
   proof.cpp
   store.cpp
   store_impl.cpp
   transaction.cpp
   transaction_impl.cpp
   transaction_participant_impl.cpp
   tree_engine.cpp
   types.cpp
)
set(FORGE_db_authenticated_DISABLED_MODULES tree_engine)
forge_target_modules_at(forge_db_authenticated db/authenticated)
target_sources(
   forge_db_authenticated PRIVATE
      FILE_SET private_modules TYPE CXX_MODULES
      BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
      FILES
         "${CMAKE_CURRENT_SOURCE_DIR}/include/forge/db/authenticated/tree_engine.cppm"
)

target_include_directories(forge_db_authenticated PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})
target_include_directories(
   forge_db_authenticated PRIVATE
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/details>"
)

target_link_libraries(
   forge_db_authenticated PUBLIC
      Boost::asio
      forge_crypto_digest
      forge_db_core
      forge_exceptions
      forge_raw
)
