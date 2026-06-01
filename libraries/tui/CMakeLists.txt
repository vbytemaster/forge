add_library(
   fcl_tui STATIC
   render.cpp
   navigation.cpp
   runner.cpp
)
fcl_target_modules(fcl_tui tui)

target_include_directories(fcl_tui PUBLIC ${FCL_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   fcl_tui
   PUBLIC
      fcl_exceptions
   PRIVATE
      ${FCL_TUI_NOTCURSES_TARGET}
)
