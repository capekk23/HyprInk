#!/usr/bin/env bash
set -euo pipefail
build_dir="$(realpath "${1:-build}")"
test_dir="$(mktemp -d)"
export XDG_RUNTIME_DIR="$test_dir/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=2
export GDK_BACKEND=wayland NO_AT_BRIDGE=1
compositor_pid= app_pid=
cleanup() {
  if [[ -n "$app_pid" ]]; then kill "$app_pid" 2>/dev/null || true; fi
  if [[ -n "$compositor_pid" ]]; then kill "$compositor_pid" 2>/dev/null || true; fi
  rm -rf "$test_dir"
}
trap cleanup EXIT
printf 'output * resolution 1280x720\n' > "$test_dir/sway.conf"
sway -c "$test_dir/sway.conf" > "$test_dir/sway.log" 2>&1 &
compositor_pid=$!
for _ in {1..50}; do
  socket="$(find "$XDG_RUNTIME_DIR" -maxdepth 1 -type s -name 'wayland-*' -print -quit)"
  if [[ -n "$socket" ]]; then break; fi
  sleep 0.1
done
if [[ -z "${socket:-}" ]]; then cat "$test_dir/sway.log"; exit 1; fi
export WAYLAND_DISPLAY="$(basename "$socket")"
export HYPRINK_SCREENSHOT="$build_dir/preview.png"
"$build_dir/hyprink-window-tests"
cat > "$test_dir/Project.conf" <<CONFIG
[app]
StoragePath = $test_dir/notes
Layer = bottom
CONFIG
"$build_dir/hyprink" --config "$test_dir/Project.conf" > "$test_dir/hyprink.log" 2>&1 &
app_pid=$!
for _ in {1..50}; do
  if [[ -n "$(find "$XDG_RUNTIME_DIR" -maxdepth 1 -type s -name 'hyprink-*.sock' -print -quit)" ]]; then break; fi
  sleep 0.1
done
# Force a flush to verify one independent state file per headless output.
"$build_dir/hyprink" --edit
"$build_dir/hyprink" --toggle
"$build_dir/hyprink" --hide
"$build_dir/hyprink" --show
"$build_dir/hyprink" --quit
wait "$app_pid" || { cat "$test_dir/hyprink.log"; exit 1; }
app_pid=
[[ $(find "$test_dir/notes" -name 'state-*.json' | wc -l) -eq 2 ]]
if rg -i 'critical|assertion|segmentation|cannot|failed' "$test_dir/hyprink.log"; then exit 1; fi
# The legacy state is copied once, with the original retained byte-for-byte.
mkdir -p "$test_dir/notes"
printf '%s' '{"version":1,"notes":[{"x":20,"y":100,"w":260,"h":64,"text":"Legacy note"}],"strokes":[]}' > "$test_dir/notes/state.json"
rm "$test_dir/notes"/state-*.json
"$build_dir/hyprink" --config "$test_dir/Project.conf" > "$test_dir/hyprink.log" 2>&1 &
app_pid=$!
sleep 1
"$build_dir/hyprink" --quit
wait "$app_pid"
app_pid=
rg 'Legacy note' "$test_dir/notes"/state-*.json
rg 'Legacy note' "$test_dir/notes/state.json"
echo 'Wayland window, IPC, two-output and legacy migration smoke tests passed'
