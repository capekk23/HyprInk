-- HyprInk bind for Hyprland 0.55+ Lua config.
-- Put this in ~/.config/hypr/hyprland.lua or require it from there.

hl.bind("SUPER + N", hl.dsp.exec_cmd("hyprink --toggle"), {
  description = "Toggle HyprInk"
})
