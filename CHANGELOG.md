# Changes

## 1.0.1

- Keep canvases visible in pass-through mode; explicit edit, show, hide and quit
  commands, a Notes/Draw toolbar and keyboard shortcuts.
- GTK input-method commits, grapheme-safe Backspace and clipboard copy/cut/paste.
- Undo/redo across text, notes, strokes, erase, movement and resizing.
- Preserve active empty notes while saving; remove them only when editing ends.
- Atomic private state files, previous-version backups, validated JSON and
  recoverable corruption without discarding the damaged original.
- Per-monitor canvases, legacy migration and monitor add/remove handling.
- Cancel unfinished gestures and release keyboard capture when changing workspace.
- Replace the blocking threaded stream socket with a main-loop datagram socket
  and a per-Wayland-session instance lock.
- Correct user-config precedence and fail on an unreadable explicit config.
- Add automated core/Wayland regression tests and CI installation artifacts.
