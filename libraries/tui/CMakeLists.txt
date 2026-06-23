add_library(
   forge_tui STATIC
   render.cpp
   navigation.cpp
   runner.cpp
)
forge_target_modules(forge_tui tui)

target_include_directories(forge_tui PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_tui
   PUBLIC
      forge_exceptions
   PRIVATE
      ${FORGE_TUI_NOTCURSES_TARGET}
)
