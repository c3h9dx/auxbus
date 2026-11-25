#!/usr/bin/env bash
# make_svg.sh — renders the diagrams in this directory to SVG.
#
#   *.edn    Bytefield source  -> <name>.svg (bytefield-svg)
#   *.json5  WaveDrom source   -> <name>.svg (wavedrom-cli)
#
# Usage:  ./make_svg.sh          renders diagrams whose source is newer than the SVG
#         ./make_svg.sh -f       renders all diagrams
#
# Requires: bytefield-svg and wavedrom-cli, or npx (Node.js).
# Optional: rsvg-convert (librsvg) flattens the SVG (text as paths, white
# background), so it looks the same on GitHub in light and dark mode.
#   macOS:  brew install node librsvg

set -euo pipefail
cd "$(dirname "$0")"

need() { command -v "$1" >/dev/null 2>&1 || { echo "missing program: $1" >&2; exit 1; }; }
tool() {   # tool <program>: installed locally, or via npx
  if command -v "$1" >/dev/null 2>&1; then echo "$1"; else need npx; echo "npx --yes $1"; fi
}
BYTEFIELD="$(tool bytefield-svg)"
WAVEDROM="$(tool wavedrom-cli)"
FLATTEN=0; command -v rsvg-convert >/dev/null 2>&1 && FLATTEN=1

force=0; [ "${1:-}" = "-f" ] && force=1
tmp="$(mktemp -t auxbus_svg)"; trap 'rm -f "$tmp"' EXIT

shopt -s nullglob
for src in *.edn *.json5; do
  svg="${src%.*}.svg"
  [ $force -eq 0 ] && [ -f "$svg" ] && [ "$svg" -nt "$src" ] && continue
  echo "→ $svg"
  case "$src" in
    *.edn)   $BYTEFIELD < "$src" > "$tmp" ;;
    *.json5) $WAVEDROM -i "$src" -s "$tmp" ;;
  esac
  [ -s "$tmp" ] || { echo "  render failed: $src" >&2; exit 1; }
  if [ $FLATTEN -eq 1 ]; then
    rsvg-convert -f svg -b white -o "$svg" "$tmp"
  else
    cp "$tmp" "$svg"
  fi
done
echo "done"
