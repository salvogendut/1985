#!/bin/bash
# tests/test_drive_b_write.sh — headless PIP B:=A:HELLO test
#
# Boots CP/M 3 from CPM3 1-07.dsk in drive A, mounts a freshly-made CF2DD
# blank in drive B, types `PIP B:HELLO.TXT=A:PROFILE.SUB`, captures an FDC
# trace and final screenshot. Exits cleanly.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-/tmp/claude-1001/-var-home-salvogendut-Dev-1985/c4476cf5-28db-47bd-9330-e24d7fafa173/scratchpad}"
BOOT="${BOOT:-$HOME/Documents/CPM Boot/CPM3 1-07.dsk}"

mkdir -p "$OUT"
# IMPORTANT: 1985 saves modified disks back to the file on disk. Always
# mount a COPY of the boot disk so the test can't corrupt the user's
# original (we learned this the hard way when TEST.TXT got deleted by
# an earlier test run that mounted "$BOOT" directly).
cp "$BOOT" "$OUT/boot-a-for-b.dsk"
"$ROOT/tools/mkblank" "$OUT/blank-b.dsk" cf2dd

BLANK_MD5_BEFORE=$(md5sum "$OUT/blank-b.dsk" | awk '{print $1}')
echo "blank-b.dsk md5 before: $BLANK_MD5_BEFORE"

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$ROOT/1985" \
        --config "$ROOT/tests/test.conf" \
        --disk-a "$OUT/boot-a-for-b.dsk" \
        --disk-b "$OUT/blank-b.dsk" \
        --paste-event "900:B:\n" \
        --paste-event "1500:A:\n" \
        --paste-event "2500:PIP B:HELLO.COM=A:DIR.COM\n" \
        --paste-event "3500:DIR B:\n" \
        --screenshot-at "4500:$OUT/drive_b.ppm" \
        --exit-after 4600 \
        2> "$OUT/fdc_b.log" || true

BLANK_MD5_AFTER=$(md5sum "$OUT/blank-b.dsk" | awk '{print $1}')
echo "blank-b.dsk md5 after:  $BLANK_MD5_AFTER"

WRITES=$(grep -c "WRITE DATA" "$OUT/fdc_b.log" || true)
echo "WRITE DATA commands in trace: $WRITES"

if [ "$BLANK_MD5_BEFORE" != "$BLANK_MD5_AFTER" ]; then
    echo "PASS: drive B blank was modified"
else
    echo "FAIL: drive B blank unchanged"
fi
