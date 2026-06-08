add_library(
   fcl_asio STATIC
   runtime.cpp
   task_scheduler.cpp
)
fcl_target_modules(fcl_asio asio)

target_include_directories(fcl_asio PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_asio PUBLIC
      Boost::asio
      fcl_exceptions
   PRIVATE
      Threads::Threads
)
