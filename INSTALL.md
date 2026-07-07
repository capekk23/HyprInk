# Install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

Default config is installed to `${CMAKE_INSTALL_PREFIX}/share/hyprink/Project.conf`.
For distro packages, the prefix should be `/usr`.

User config path:

```sh
mkdir -p ~/.config/hyprink
cp /usr/share/hyprink/Project.conf ~/.config/hyprink/Project.conf
```
