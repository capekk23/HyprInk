# HyprInk 1.0.1 release checks

The repository previously declared version 1.0. This update uses 1.0.1 to avoid
replacing an existing release tag. A tag should be created only after CI passes
and the physical-desktop checks below have been recorded.

## Automated

Passed on Ubuntu 24.04 / GTK 3.24.41 / gtk-layer-shell 0.8.2, 2026-10-01.
[Verified CI run](https://github.com/capekk23/HyprInk/actions/runs/36841133973).
The captured preview was visually checked for note sizing and Unicode rendering.

- C++17 build with compiler warnings enabled.
- Core tests: UTF-8/graphemes, history invalidation/bounds, JSON round-trip,
  actively edited blank notes, atomic writes, 0600 permissions, previous-state
  backups, corrupt recovery and preservation, failed-write cleanup.
- Real GTK/layer-shell event scenarios on headless Sway: note creation, IME
  commit, clipboard, undo/redo, strokes, erase, drag, cancellation and layer/
  keyboard-mode transitions.
- Two-output startup and one state file per output, CLI IPC, clean quit,
  first-run migration retaining legacy state, and staged installation.

## Physical Hyprland desktop (pending)

- Start in passive mode; click through notes to desktop/windows and type into
  another application. Enter editing on each monitor with Super+N, then Escape.
- Use a Czech keyboard's dead keys and an IME. Type `Příliš žluťoučký kůň`,
  paste multiline text and emoji, backspace, undo and redo.
- Create an empty note, press Backspace, then type; the editor must remain active.
- Draw, erase, undo, move and resize. Leave the workspace mid-drag and confirm
  the unfinished gesture is cancelled, the keyboard is released, and notes
  remain available on other workspaces.
- Try a 49-inch display plus a second monitor, mixed scales and negative output
  positions. Unplug/replug an output and check its persisted canvas.
- Restart HyprInk and log out/in; verify persistence. Simulate unwritable storage
  and confirm the visible error without losing the last valid on-disk state.
- Capture a short real-desktop demo GIF for the README.
- Build/install the Arch recipe with `makepkg -si` and verify dependencies.

Known scope: canvas shared across workspaces; append-only note text; no tablet
pressure or partial text selection; drawings are not rescaled after resolution
changes. These are documented limits, not verified features.
