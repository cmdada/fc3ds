#!/usr/bin/env sh
# Bake the Google Fonts family list into romfs, one "<category>\t<family>" per
# line, sorted by family. Google's 2.6 MB index is far too slow to parse on the
# console; a stale list is harmless because the Add tab takes a family name
# directly.
#
#   tools/mkgooglefonts.sh [output]

set -eu

OUT="${1:-romfs/googlefonts.tsv}"
SRC="https://fonts.google.com/metadata/fonts"

echo "fetching $SRC"

TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT

curl -fsSL --max-time 120 "$SRC" -o "$TMP"

python3 - "$TMP" "$OUT" <<'PY'
import json, sys

src, out = sys.argv[1], sys.argv[2]

with open(src) as f:
    data = json.load(f)

rows = []
for entry in data.get("familyMetadataList", []):
    family = entry.get("family", "").strip()
    if not family:
        continue

    # The converter rasterises a Latin range, so a Devanagari- or Thai-only
    # family would produce an empty font.
    subsets = entry.get("subsets") or list((entry.get("fonts") or {}).keys())
    if subsets and not any(s.startswith("latin") for s in subsets):
        continue

    # Noto's several hundred script-specific faces would swamp a scrollable list.
    if entry.get("isNoto") and not family.startswith(("Noto Sans", "Noto Serif")):
        continue

    category = (entry.get("category") or "other").replace("\t", " ")
    rows.append((family, category))

rows.sort(key=lambda r: r[0].lower())

with open(out, "w") as f:
    for family, category in rows:
        f.write(f"{category}\t{family}\n")

print(f"wrote {len(rows)} families to {out}")
PY
