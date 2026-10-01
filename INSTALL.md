# Installation

Requires a Wayland compositor supporting `zwlr_layer_shell_v1`. Intended for
Hyprland. X11 is not supported. Build dependencies are CMake >=3.16, a C++17
compiler, pkg-config, gtkmm3, gtk-layer-shell and jsoncpp.

## Arch / CachyOS

```sh
sudo pacman -S --needed base-devel cmake pkgconf gtkmm3 gtk-layer-shell jsoncpp git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Or use the supplied VCS recipe (this is not a claim that the package is published
on AUR):

```sh
cd packaging/aur
makepkg -si
```

## Debian / Ubuntu

```sh
sudo apt install build-essential cmake pkg-config libgtkmm-3.0-dev libgtk-layer-shell-dev libjsoncpp-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

## User config and startup

For a `/usr` install:

```sh
mkdir -p ~/.config/hyprink
cp /usr/share/hyprink/Project.conf ~/.config/hyprink/Project.conf
```

For a default `/usr/local` install, copy from
`/usr/local/share/hyprink/Project.conf` instead.

In `hyprland.conf`:

```ini
exec-once = hyprink
bind = SUPER, N, exec, hyprink --toggle
```

Start with `hyprink`; it shows saved notes in pass-through mode. Use Super+N to
edit. Escape returns keyboard and mouse control to the desktop.

If an older HyprInk is running, stop it before starting this version. Configuration
changes require `hyprink --quit` and a new launch. A second invocation controls
the existing instance and does not reload its config.

## Safe upgrade and recovery

Back up `~/.local/share/hyprink` before upgrading. The legacy `state.json` is
copied once to the first detected monitor's canvas without modifying the original.

A damaged canvas recovers its valid `.bak` while preserving the damaged bytes
under `.corrupt.*`. If both files are unusable, saving is disabled for that
canvas and the editor displays a message. Stop HyprInk, copy the damaged files
somewhere safe, then restore a known-good version or move the unusable files
away to start an empty canvas.
