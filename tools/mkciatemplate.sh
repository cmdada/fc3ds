#!/usr/bin/env sh
# Extract the ticket, TMD and certificate chain the on-console CIA builder
# patches or copies verbatim, lifting them out of a CIA makerom already built.
# Re-run after changing the RSF or updating makerom.
#
#   tools/mkciatemplate.sh [source.cia] [outdir]

set -eu

SRC="${1:-fc3ds.cia}"
OUT="${2:-romfs/cia}"

[ -f "$SRC" ] || { echo "mkciatemplate: $SRC not found -- run 'make cia' first" >&2; exit 1; }

mkdir -p "$OUT"

python3 - "$SRC" "$OUT" <<'PY'
import struct, sys, os

src, out = sys.argv[1], sys.argv[2]
d = open(src, "rb").read()

hdr, ctype, ver, cert, tik, tmd, meta = struct.unpack_from("<IHHIIII", d, 0)
if hdr != 0x2020:
    sys.exit("not a CIA")

def align(v):
    return (v + 63) & ~63

cert_off = align(0x2020)
tik_off = align(cert_off + cert)
tmd_off = align(tik_off + tik)

sections = {
    "certs.bin": d[cert_off:cert_off + cert],
    "ticket.bin": d[tik_off:tik_off + tik],
    "tmd.bin": d[tmd_off:tmd_off + tmd],
}

for name, blob in sections.items():
    path = os.path.join(out, name)
    with open(path, "wb") as f:
        f.write(blob)
    print(f"{path}: {len(blob)} bytes")
PY
