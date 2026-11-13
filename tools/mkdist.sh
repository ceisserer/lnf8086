#!/bin/sh
# Build the distribution zip.
#   tools/mkdist.sh VERSION [OUTDIR]     -> OUTDIR/lfntsr-VERSION.zip
# The zip holds DOS-friendly 8.3 names with CRLF text files:
#   LFNTSR.COM, LT.COM, README.TXT, LICENSE.TXT
set -e
top=$(cd "$(dirname "$0")/.." && pwd)
ver=${1:?usage: mkdist.sh VERSION [OUTDIR]}
out=${2:-$top/build}
mkdir -p "$out"
out=$(cd "$out" && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
make -s -C "$top" all
cp "$top/lfntsr.com" "$stage/LFNTSR.COM"
cp "$top/lt.com" "$stage/LT.COM"
sed 's/$/\r/' "$top/README.md" > "$stage/README.TXT"
sed 's/$/\r/' "$top/LICENSE" > "$stage/LICENSE.TXT"
zip="$out/lfntsr-$ver.zip"
rm -f "$zip"
# fixed timestamps for reproducible archives
touch -d "@${SOURCE_DATE_EPOCH:-$(git -C "$top" log -1 --format=%ct 2>/dev/null || date +%s)}" "$stage"/*
(cd "$stage" && zip -q -X "$zip" LFNTSR.COM LT.COM README.TXT LICENSE.TXT)
echo "$zip"
