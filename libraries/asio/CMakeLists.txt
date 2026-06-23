add_library(
   forge_asio STATIC
   runtime.cpp
   task_scheduler.cpp
)
forge_target_modules(forge_asio asio)

target_include_directories(forge_asio PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_asio PUBLIC
      Boost::asio
      forge_exceptions
   PRIVATE
      Threads::Threads
)
