add_library(
   forge_app_tui STATIC
   dashboard.cpp
)
forge_target_modules(forge_app_tui app_tui)

target_include_directories(forge_app_tui PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_app_tui PUBLIC
      forge_app
      forge_tui
      Boost::asio
)
