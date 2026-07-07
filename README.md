# HyprInk

Persistent desktop notes and scribbles for Hyprland.

## Compatibility Target

- Hyprland `0.55+`
- Lua-based Hyprland config
- External Wayland layer-shell app, not a Hyprland plugin

This keeps HyprInk away from Hyprland plugin ABI breakage and lets the shortcut live in `hyprland.lua`.

Example bind:

```lua
hl.bind("SUPER + N", hl.dsp.exec_cmd("/home/karel/Documents/Github/HyprInk/build/hyprink --toggle"), {
  description = "Toggle HyprInk"
})
```

See `examples/hyprland.lua`.

## Goal

HyprInk is a toggleable desktop overlay for writing directly on the active Hyprland workspace.

Target workflow:

- Press a Lua-configured shortcut, for example `SUPER + N`, to toggle HyprInk on the current workspace.
- Draw by holding left mouse button and moving the pointer.
- Click anywhere to create a text note.
- Drag a note by grabbing anywhere inside its rectangle.
- Double-click a note to edit its text.
- Keep all scribbles and notes persistent.

## Current Features

- C++ GTK/layer-shell app
- `hyprink --toggle` single-instance toggle
- Mouse scribbling with left click + drag
- Click empty desktop overlay space to create a text note
- Drag notes by grabbing inside their rectangle
- Double-click notes to edit
- Delete notes or full strokes with the configured delete button
- Persistent JSON storage per active Hyprland workspace
- Configurable background, layer, controls, stylus, and note styling

## Planned Features

- Better text cursor and selection behavior
- Undo/redo
- Eraser mode
- Multiple monitors
- Built-in install helper

## Build

Dependencies:

- C++17 compiler
- CMake
- `gtkmm-3.0`
- `gtk-layer-shell`
- `jsoncpp`

Build:

```sh
cmake -S . -B build
cmake --build build
```

Run:

```sh
./build/hyprink --toggle
```

Press `Esc` or `Ctrl+Enter` to finish editing a note.

For the current local checkout, the command used by Hyprland is:

```sh
/home/karel/Documents/Github/HyprInk/build/hyprink --toggle
```

## Config

Initial config lives in `Project.conf`.

```conf
[app]
AppToggle = SUPER+N
StoragePath = ~/.local/share/hyprink
Layer = bottom

[appearance]
BackgroundMode = transparent
BlackBackgroundColor = #000000

[controls]
DrawButton = left
DeleteButton = middle
DeleteDistance = 14

[drawing]
StylusSize = 4
StylusColor = #ff3355

[notes]
Font = monospace
FontSize = 16
TextColor = #ffffff
BackgroundColor = #00000099
BorderColor = #ffffffcc
BorderWidth = 1
Padding = 8
Width = 260
MinHeight = 64
```

## Status

First runnable prototype.
