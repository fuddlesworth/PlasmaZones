#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f552-copied-layout-zone-ids: zone ids are unique across layouts, so a copied layout's zones get
# new ids. Runner: 1 output, flag S. Restarts the nested daemon itself.
# Staging: the repo's columns-3.json (COLS3) is copied into the nested user layouts directory as
#   p13-copy.json with a new layout id and name AND its zone numbers 1 and 3 swapped (each zone keeps
#   its geometry), so a merged id map's last write would show in a layout switch. The daemon is
#   restarted (step 2), then restarted again (step 4). Step 5: quick slots 1 = COLS3, 2 = the copy;
#   p1 is snapped into COLS3 zone 1 (left column), quick_layout_2 switches to the copy, quick_layout_1
#   back. Step 6: COLS3's zones under another fresh layout id are imported with importLayout.
# Asserts: step 2 logs one "Layout file .../p13-copy.json reused 3 zone id(s) of another layout - gave
#   them new ids", the copy's zone ids share none with COLS3's, and the copy's id-named file on disk
#   holds the new ids; step 4 logs no "reused" line and the ids stay; step 5 p1 goes left, right (the
#   copy's zone number 1), left (COLS3 zone 1); step 6 logs "importLayout: 3 zone id(s) of another
#   layout given new ids" and the import's ids share none with COLS3's.
# A FAIL means: the copy kept COLS3's zone ids, so the shared ids made the layout round trip move p1.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
COPYID="{3e7b2a14-55c1-4d0e-9a6f-0c1d2e3f4a51}"
IMPID="{9b1c4d2e-77a0-4f3b-8c5d-6e7f8091a2b3}"
ZONE1="{2bb43b1d-3506-40b7-8f2b-e0fe59c0e90e}"
LDIR="$XDG_DATA_HOME/plasmazones/layouts"
COPY="$LDIR/p13-copy.json"
COPYFILE="$LDIR/${COPYID//[\{\}]/}.json"
IMPORT="$NEST/p13-import.json"
# zoneids -> "number:id ..." sorted by zone number, from a layout JSON on stdin
zoneids() {
    python3 -c '
import json, sys
try:
    d = json.load(sys.stdin)
except Exception as e:
    print("unparsable:", str(e)[:60]); sys.exit()
print(" ".join("%s:%s" % (z.get("zoneNumber"), z.get("id", "")) for z in sorted(d.get("zones", []), key=lambda z: z.get("zoneNumber", 0))))'
}
# shared A B -> how many zone ids two zoneids answers have in common
shared() {
    python3 -c '
import sys
a = {t.split(":", 1)[1] for t in sys.argv[1].split() if ":" in t}
b = {t.split(":", 1)[1] for t in sys.argv[2].split() if ":" in t}
print(len(a & b))' "$1" "$2"
}
lzones() { PZ org.plasmazones.LayoutRegistry.getLayout "$1" | zoneids; }
kwjs_ok
read -r _ _ SW _ <<< "$(scr Virtual-0)"
echo "== step 2: the daemon restarted with the copy in the user directory"
dkill
mkdir -p "$LDIR"
python3 -c '
import json, sys
src, copy, imp, copyid, impid = sys.argv[1:6]
d = json.load(open(src))
c = dict(d, id=copyid, name="P13 Copy")
zones = []
for z in d["zones"]:
    z = dict(z)
    if z.get("zoneNumber") == 1:
        z["zoneNumber"] = 3
    elif z.get("zoneNumber") == 3:
        z["zoneNumber"] = 1
    zones.append(z)
c["zones"] = zones
json.dump(c, open(copy, "w"), indent=2)
json.dump(dict(d, id=impid, name="P13 Import"), open(imp, "w"), indent=2)
' "$PZR_REPO/plasmazones/data/layouts/columns-3.json" "$COPY" "$IMPORT" "$COPYID" "$IMPID" \
    || stage_fail "could not write the copy and the import file"
drestart
sleep 4
check "step 2: one 'reused' line for p13-copy.json" \
    "$(grep -cE "Layout file \".*p13-copy\.json\" reused 3 zone id\(s\) of another layout - gave them new ids" "$NEST/daemon.log")" = 1
C3=$(lzones "$COLS3")
CP=$(lzones "$COPYID")
echo "  COLS3 zones: $C3"
echo "  copy zones:  $CP"
[ "$(echo "$C3" | wc -w)" = 3 ] && [ "$(echo "$CP" | wc -w)" = 3 ] || stage_fail "COLS3 or the copy did not load with three zones"
check "step 2: the copy shares no zone id with COLS3" "$(shared "$C3" "$CP")" = 0
DISK=$( [ -f "$COPYFILE" ] && zoneids < "$COPYFILE")
echo "  the copy's id-named file: ${DISK:-(missing)}"
check "step 2: the copy's file on disk holds the new ids" "$DISK" = "$CP"
echo "== step 4: restarted again"
dkill
drestart
sleep 4
check "step 4: no 'reused' line" "$(grep -cE "reused .* zone id\(s\) of another layout" "$NEST/daemon.log")" = 0
check "step 4: the copy keeps its ids" "$(lzones "$COPYID")" = "$CP"
echo "== step 5: a layout round trip through the copy"
setting snapAssistEnabled false
PZ org.plasmazones.LayoutRegistry.setQuickLayoutSlot 0 1 "$COLS3" >/dev/null
PZ org.plasmazones.LayoutRegistry.setQuickLayoutSlot 0 2 "$COPYID" >/dev/null
assign Virtual-0 "$COLS3"
sleep 2
open p1 400 300
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
# column CAPTION -> LEFT, MIDDLE or RIGHT third of the output, by the window's centre
column() {
    local x w c
    read -r x _ w _ _ <<< "$(geo "$1")"
    c=$((${x%.*} + ${w%.*} / 2))
    if [ "$c" -lt $((SW / 3)) ]; then echo LEFT; elif [ "$c" -lt $((2 * SW / 3)) ]; then echo MIDDLE; else echo RIGHT; fi
}
quick() {
    shortcut "quick_layout_$1"
    sleep 2.5
}
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-0 >/dev/null
sleep 2
[ "$(column p1)" = LEFT ] && [ "$(zoneof "$P1")" = "$ZONE1" ] || stage_fail "p1 not in COLS3 zone 1 ($(geo p1))"
quick 2
echo "  after quick_layout_2: $(geo p1) zone=$(zoneof "$P1")"
check "step 5: the copy puts p1 in its zone number 1, the right column" "$(column p1)" = RIGHT
quick 1
echo "  after quick_layout_1: $(geo p1) zone=$(zoneof "$P1")"
check "step 5: back on COLS3, p1 is in the left column" "$(column p1)" = LEFT
check "step 5: back on COLS3, p1 is in COLS3 zone 1" "$(zoneof "$P1")" = "$ZONE1"
echo "== step 6: import COLS3's zones under a fresh layout id"
dmark
NEWID=$(PZ org.plasmazones.LayoutRegistry.importLayout "$IMPORT")
echo "  importLayout -> $NEWID"
check "step 6: the import re-issues COLS3's zone ids" \
    "$(dlog "importLayout: 3 zone id\\(s\\) of another layout given new ids" | wc -l)" -ge 1
IM=$(lzones "$NEWID")
echo "  imported zones: $IM"
[ "$(echo "$IM" | wc -w)" = 3 ] || stage_fail "the import did not load with three zones"
check "step 6: the import shares no zone id with COLS3" "$(shared "$C3" "$IM")" = 0
finish
