#!/usr/bin/env sh
# Stand-in for the Palace (gds2palace) Python in tests: never runs gds2palace / gmsh.
# Writes an empty config.json where gds2palace would (palace_model/<model>_data), so the
# solver stage finds a run directory. Gets the model script path as $1.
script="$1"
dir=$(dirname "$script")
base=$(basename "$script" .py)
mkdir -p "$dir/palace_model/${base}_data" && echo '{}' > "$dir/palace_model/${base}_data/config.json"
exit 0
