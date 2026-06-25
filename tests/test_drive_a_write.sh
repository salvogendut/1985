#!/bin/bash
# tests/test_drive_a_write.sh — control: PIP A:NEW.TXT=A:PROFILE.SUB on
# the original boot disk (writing back to drive A is known to work today,
# so its FDC trace is the reference shape).
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-/tmp/claude-1001/-var-home-salvogendut-Dev-1985/c4476cf5-28db-47bd-9330-e24d7fafa173/scratchpad}"
BOOT="${BOOT:-$HOME/Documents/CPM Boot/CPM3 1-07.dsk}"

mkdir -p "$OUT"
cp "$BOOT" "$OUT/boot-a.dsk"
"$ROOT/tools/mkblank" "$OUT/blank-a.dsk" cf2dd

BLANK_MD5_BEFORE=$(md5sum "$OUT/blank-a.dsk" | awk '{print $1}')
echo "blank-a.dsk md5 before: $BLANK_MD5_BEFORE"

# Boot from CPM3 1-07 in drive A, then SWAP (CCP C command) to blank.
# Simpler: mount boot disk in drive A and blank as drive B, then COPY from
# A to B — wait, that's exactly the drive B test. For an A-side write
# control we PIP back to A:. The boot disk is full, so we ERA something
# first to make space.
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$ROOT/1985" \
        --config "$ROOT/tests/test.conf" \
        --disk-a "$OUT/boot-a.dsk" \
        --paste-event "900:ERA A:RPED.SUB\n" \
        --paste-event "1500:PIP A:HELLO.TXT=A:TEST.TXT\n" \
        --paste-event "2400:DIR A:HELLO.TXT\n" \
        --screenshot-at "2700:$OUT/drive_a.ppm" \
        --exit-after 2800 \
        2> "$OUT/fdc_a.log" || true

WRITES=$(grep -c "WRITE DATA" "$OUT/fdc_a.log" || true)
echo "WRITE DATA commands in trace: $WRITES"
