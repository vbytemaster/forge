# forge_tui

`forge_tui` is a reusable terminal UI component library. It provides value models,
deterministic render helpers, navigation and a Notcurses-backed screen runner.
Notcurses is a backend detail and does not appear in public module interfaces.

## When To Use

- Build a terminal operator console with tables, forms, navigation and event
  logs.
- Test UI rendering deterministically without a real terminal.
- Need terminal capability detection and a graceful degraded/headless mode.

## When Not To Use

- Do not put application authority checks in UI. UI can hide/disable actions, but
  the consuming system must enforce permissions.
- Do not use Notcurses types outside `libraries/tui/*.cpp`.
- Do not add browser/web UI assumptions here.

## Public Modules

- `forge.tui.types` — value models, statuses, action states, terminal capabilities.
- `forge.tui.render` — deterministic text renderers and redaction helpers.
- `forge.tui.navigation` — `navigation_stack`.
- `forge.tui.runner` — `screen_runner`, input events, capability detection.

Target: `forge_tui`.

Dependencies: Notcurses core privately. If `FORGE_ENABLE_TUI=ON` but Notcurses is
not found, TUI targets are skipped with a clear CMake status message; non-TUI
libraries continue to build.

## Examples

### Render A Status Badge

```cpp
import forge.tui.render;
import forge.tui.types;

auto lines = forge::tui::render_status_badge({
   .value = forge::tui::status::degraded,
   .label = "storage",
});
```

### Redact Key/Value Panels

```cpp
import forge.tui.render;

auto panel = forge::tui::render_key_value_panel({
   {.key = "endpoint", .value = "https://user:pass@example.test/api"},
   {.key = "token", .value = "secret", .sensitive = true},
});
```

### Validate A Form

```cpp
import forge.tui.render;
import forge.tui.types;

auto form = forge::tui::form_model{{
   {.name = "profile", .label = "Profile", .required = true},
}};
auto validation = forge::tui::validate_form(form);
```

### Headless Runner Test

```cpp
import forge.tui.runner;

auto runner = forge::tui::screen_runner{};
auto code = runner.run({
   .headless = true,
   .input = [] { return forge::tui::input_event{forge::tui::input_event::kind::quit}; },
   .model = [] { return forge::tui::shell_model{.title = "FORGE Control"}; },
});
```

## Security Notes

Rendering helpers redact common credential patterns and fields marked sensitive,
but callers must still avoid passing private keys or tokens into generic
strings. UI is a presentation boundary, not a security boundary.

## Risks And Anti-Patterns

- Do not treat hidden or redacted UI text as access control. Authority belongs
  to the application/service layer.
- Do not render secret-bearing generic strings and hope endpoint redaction will
  catch every format. Mark sensitive fields explicitly.
- Do not let terminal rendering perform network, filesystem or app lifecycle
  work. Render helpers must stay deterministic and testable.

## Typical Mistakes

- Do not rely on colors/unicode being available; inspect `terminal_capabilities`.
- Do not make render helpers perform I/O. They return stable lines for tests.
- Do not couple TUI models to app/plugin/runtime internals.

## Tests

`test_forge_tui` covers status badges, table states, key/value and endpoint
redaction, form validation, shell/event log rendering, navigation, dangerous and
disabled action states, headless runner quit and terminal capability failure.
