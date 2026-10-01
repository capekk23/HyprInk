# HyprInk

Persistent notes and freehand drawing on your Hyprland desktop. A small C++17
application using GTK3 and Wayland layer-shell; MIT licensed.

## What you can do

- Leave notes and drawings visible while mouse clicks and keyboard input pass
  through to your desktop. `hyprink --toggle` enters or leaves editing.
- Switch between **Notes** and **Draw** using the toolbar or F1/F2.
- Type Czech and other Unicode text through GTK's input method. Backspace
  removes a complete grapheme, including emoji and combining accents.
- Paste text, undo/redo typing, drawing, erasing, moving and resizing.
- Keep separate canvases for each monitor, restored after restart.
- Save through an atomic replacement with a previous-version backup. A damaged
  file is preserved when its valid backup can be recovered.

## Install

See [INSTALL.md](INSTALL.md) for dependencies, building and the Arch package recipe.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
hyprink
```

Add this to your Hyprland config:

```ini
exec-once = hyprink
bind = SUPER, N, exec, hyprink --toggle
```

A Lua example is retained under `examples/` for installations using that config
format; use the syntax supported by your own Hyprland version.

## Controls

| Action | Control |
| --- | --- |
| Enter/leave editing on the focused monitor | `hyprink --toggle` / Super+N with the bind above |
| Create a note | Notes tool, click an empty spot |
| Edit an existing note | Click without dragging, or double-click |
| Draw a stroke or dot | Draw tool, left mouse button |
| Move a note | Left-drag it |
| Resize a note | Right-drag it |
| Erase a note or stroke | Middle-click it |
| Switch tools | F1 Notes / F2 Draw (outside text editing) |
| Paste text into the current note | Ctrl+V |
| Copy / cut the entire current note | Ctrl+C / Ctrl+X |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y |
| Finish text editing, stay on the canvas | Ctrl+Enter |
| Return to desktop pass-through | Escape, Ctrl+Q or Done |
| Show / hide all canvases | `hyprink --show` / `hyprink --hide` |
| Save and stop the application | `hyprink --quit` |

The compact text editor appends at the end of a note. Cursor positioning and
partial text selection are not implemented. Undo/redo is kept in memory,
limited to 100 snapshots and about 32 MiB per stack; it resets after restart.

## Configuration and data

`hyprink --config /path/to/Project.conf` uses exactly that file and fails clearly
if it cannot be read. Otherwise lookup order is:

1. `$XDG_CONFIG_HOME/hyprink/Project.conf` (or `~/.config/hyprink/Project.conf`).
2. `Project.conf` in the current directory.
3. `/usr/local/share/hyprink/Project.conf`, then `/usr/share/hyprink/Project.conf`.
4. Built-in defaults.

`AppToggle` is a reminder for your compositor bind, not a global shortcut
registered by the application. Mouse buttons, colors, fonts and storage path
are configurable. A black background applies only while editing. Blur should
be configured in the compositor; it is not an application background mode.

The default data folder is `~/.local/share/hyprink`. Each Hyprland connector
gets `state-<connector>.json` and a `.bak` file. Legacy `state.json` is copied to
the first detected monitor once and remains untouched. Other layer-shell
compositors use the monitor model and enumeration index as a fallback identity.
Keep a copy of the entire data folder when migrating to a different machine.

Canvases are shared across workspaces on each monitor. Leaving the workspace
where editing started returns to pass-through. Coordinates use logical pixels;
notes are clamped to the available monitor size rather than rescaled. Drawings
keep their original coordinates. The editor shows a save error if storage is
unavailable; it never silently overwrites an unreadable state without a valid
backup. Damaged originals are retained as `.corrupt.*` files.

## Development and verification

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build -R '^core$' --output-on-failure
bash tests/wayland-smoke.sh build # needs sway, grim and ripgrep; run as a normal user
```

CI builds the application and tests Unicode editing, empty-note preservation,
undo/redo, clipboard, restart persistence, corrupt-state recovery, IPC, legacy
migration and two headless outputs. It uploads a preview and staged installation.
See [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md) for the remaining
checks on a physical Hyprland desktop before tagging a release.

Unofficial project; not affiliated with Hyprland Development.
