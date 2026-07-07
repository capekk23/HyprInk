-- HyprInk bind for Hyprland 0.55+ Lua config.
-- Put this in ~/.config/hypr/hyprland.lua or require it from there.

hl.bind("SUPER + N", hl.dsp.exec_cmd("/home/karel/Documents/Github/HyprInk/build/hyprink --toggle --config /home/karel/Documents/Github/HyprInk/Project.conf"), {
  description = "Toggle HyprInk"
})
