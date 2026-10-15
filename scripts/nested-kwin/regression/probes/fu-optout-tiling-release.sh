#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout-tiling-release: a window a tiling engine releases into the tiling opt-out
# (autotile:none) does not get its old snap float back. Runner: 1 output, no flags;
# PZ_FU_RELEASE_FROM=bsp (row fu-optout-tiling-release-bsp) or scroll (row
# fu-optout-tiling-release-scroll).
# Staging: Snap Assist off, Virtual-0 COLS3. w opens and is floated with the float shortcut (a snap
#   float). Virtual-0 is assigned autotile:bsp (or scrolling), where w becomes a tile (or a
#   column), then desktop 1 is assigned autotile:none, so the engine releases w into the opt-out.
# Asserts: no "windowsReleased: restoring snap-float for <w>" line, at least one
#   "dropOptOutFloat: <w> holds no float on the tiling opt-out" line, isWindowFloating w false and
#   w not in getFloatingWindows.
# On main (8ca183308): the release logs "restoring snap-float" for w and broadcasts it floating.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
FROM="${PZ_FU_RELEASE_FROM:-bsp}"
case "$FROM" in
bsp) ENGINE="autotile:bsp" ;;
scroll) ENGINE="scrolling:" ;;
*) stage_fail "unknown PZ_FU_RELEASE_FROM '$FROM'" ;;
esac
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 1.5
open w
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
floatkey w
echo "snap float: $(wstat w "$W")"
[ "$(floating "$W")" = true ] || stage_fail "the float shortcut did not float w on COLS3"
echo "== Virtual-0 assigned $ENGINE"
assign Virtual-0 "$ENGINE"
sleep 3
if [ "$FROM" = bsp ]; then
    echo "  w $(geo w) | tiles $(tilelist Virtual-0)"
    case "$(tilelist Virtual-0)" in *"${WU: -8}"*) ;; *) stage_fail "w did not become a tile under $ENGINE" ;; esac
else
    echo "  w $(geo w) | strip $(stripx)"
    case "$(stripx)" in *"[w "*) ;; *) stage_fail "w did not become a column under $ENGINE" ;; esac
fi
echo "== desktop 1 assigned autotile:none"
dmark
assignd Virtual-0 1 "autotile:none"
sleep 2.5
echo "  $(wstat w "$W")"
dlog "windowsReleased: .*$WU.*|dropOptOutFloat: .*$WU.*" | sed 's/^/    /'
check "'restoring snap-float' lines for w" "$(dlog "windowsReleased: restoring snap-float for [^ ]*$WU" | wc -l)" -eq 0
check "dropOptOutFloat lines for w" "$(dlog "dropOptOutFloat: [^ ]*$WU[^ ]* holds no float on the tiling opt-out" | wc -l)" -ge 1
check "isWindowFloating w on the opt-out" "$(floating "$W")" = false
check "getFloatingWindows entries for w" "$(floatlisted "$W")" -eq 0
finish
