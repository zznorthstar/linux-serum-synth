#!/usr/bin/env bash
# Installs locally extracted Serum 2 content (wavetables, noises, samples, multisamples,
# impulses, LFO shapes/paths, curves, arp/clip banks, effect chains, presets ...) into the
# ZYG-ZXG content library that the plugin resolves assets from, like Serum's own folders.
# Nothing here is committed or packaged; the content is the user's licensed copy.
#   usage: tools/install_serum_content.sh [extracted-dir] [dest]
# The extracted dir comes from: 7z x "Install_Xfer_Serum2_*.exe" -o.local-serum-content
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
src="${1:-$here/.local-serum-content}"
dest="${2:-${ZYGZXG_CONTENT:-${XDG_DATA_HOME:-$HOME/.local/share}/ZYG-ZXG/Content}}"
[ -d "$src/Tables" ] || { echo "no Serum content at $src" >&2; exit 1; }
mkdir -p "$dest"
for d in "$src"/*/; do
  name="$(basename "$d")"
  echo "installing $name"
  cp -a --reflink=auto "$d" "$dest/"
done
mkdir -p "$dest/Presets/User"
for p in "$here"/*.SerumPreset; do [ -e "$p" ] && cp --update=none "$p" "$dest/Presets/User/"; done
echo "content library: $dest ($(find "$dest" -type f | wc -l) files)"
