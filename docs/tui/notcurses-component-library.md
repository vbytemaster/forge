# TUI Component Library

`fcl_tui` is a reusable terminal UI library over Notcurses core. Notcurses is
the rendering/input backend; FCL owns models, rendering contracts, navigation
and redaction rules.

Local guide: [libraries/tui/README.md](../../libraries/tui/README.md).

## Задача

Operator tools need terminal UIs that are pleasant enough for daily use, but
also testable and safe. Direct backend widgets make logic hard to test and can
turn UI events into accidental business flow. FCL chooses value models first:
rendering can be deterministic without a terminal, while runner code owns the
backend session.

## Public Model

```text
value models
  -> render helpers produce stable lines
  -> navigation stack updates selected route
  -> screen_runner handles terminal/headless loop
  -> consuming program owns actions and authority checks
```

The UI library does not know about application plugins, network protocols,
storage, billing or any product-specific roles.

## Components

- Status badges for `ok`, `degraded`, `blocked`, `offline`, `unknown`.
- Key/value panels with sensitive field support.
- Table/list view with empty/loading/error states.
- Forms with required-field validation and field errors.
- Modal/action bar/event log rendering.
- Navigation stack and selected item state.
- Shell model for a common operator-console layout.

## Backend Boundary

- Notcurses headers appear only in implementation files such as
  `libraries/tui/runner.cpp`.
- Public modules expose no `notcurses`, `ncplane` or terminal backend handles.
- `screen_runner_options::headless` is first-class for tests.
- Terminal capability detection returns degraded state rather than leaking
  backend initialization exceptions.

## Security Boundary

UI is not authority. It may hide/disable actions based on caller-provided state,
but the system behind the UI must still enforce permissions. Redaction helpers
are defensive presentation tools, not a secret management layer.

## Donor Decisions

Accepted:

- Notcurses core lifecycle and input/render backend.
- FTXUI-style component composition ideas.
- k9s-style resource list/detail navigation.
- Ceph/Syncthing-style health visibility and degraded reasons.

Rejected:

- Backend terminal types in public API.
- Multimedia Notcurses dependency in v1.
- UI event bus as business-flow transport.
- UI as security boundary.

## Verification

`test_fcl_tui` covers stable text rendering, redaction, forms, navigation,
disabled/dangerous actions, event logs, headless runner quit and terminal
capability failure.
