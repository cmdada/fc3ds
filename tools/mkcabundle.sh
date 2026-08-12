#!/usr/bin/env bash
#
# Regenerate romfs/cacert.pem from the current Mozilla trust store.
#
# mbedTLS reparses the whole bundle per TLS handle, which takes seconds on an
# ARM11, so only the CA families our endpoints chain to are kept. Filtering by
# family rather than by exact root survives root rotations. Widen KEEP_PATTERN
# if a host starts failing with a certificate error.
set -euo pipefail

cd "$(dirname "$0")/.."

FULL=tools/cacert-full.pem
OUT=romfs/cacert.pem

KEEP_PATTERN='ISRG|GTS Root|GlobalSign|DigiCert|Sectigo|USERTrust|COMODO|Amazon|Baltimore'

echo "Fetching current Mozilla store..."
curl -sSL --max-time 60 -o "$FULL" https://curl.se/ca/cacert.pem

echo "Filtering to: $KEEP_PATTERN"
awk -v pat="$KEEP_PATTERN" '
	# Root labels in curl.se bundles are a bare line followed by a rule of "=".
	/^=+$/ { label = prev; next }
	/^-----BEGIN CERTIFICATE-----$/ {
		keep = (label ~ pat)
		if (keep) print label "\n" gensub(/./, "=", "g", label)
		body = ""
	}
	/^-----BEGIN CERTIFICATE-----$/, /^-----END CERTIFICATE-----$/ {
		if (keep) print
		if ($0 ~ /^-----END CERTIFICATE-----$/) { keep = 0; print "" }
		prev = $0
		next
	}
	{ prev = $0 }
' "$FULL" > "$OUT"

before=$(grep -c 'BEGIN CERTIFICATE' "$FULL")
after=$(grep -c 'BEGIN CERTIFICATE' "$OUT")
echo "Roots: $before -> $after   ($(du -h "$OUT" | cut -f1))"

echo
echo "Verifying live endpoints against the trimmed bundle..."
fail=0
for u in \
	"https://fonts.googleapis.com/css2?family=Roboto" \
	"https://fonts.gstatic.com/"
do
	host=$(echo "$u" | awk -F/ '{print $3}')
	if curl -sS --cacert "$OUT" -o /dev/null --max-time 25 "$u"; then
		echo "  ok    $host"
	else
		echo "  FAIL  $host"
		fail=1
	fi
done

[ "$fail" -eq 0 ] || { echo; echo "Verification failed - widen KEEP_PATTERN."; exit 1; }
echo
echo "Wrote $OUT"
