#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1030-parked-ratio-returns: a parked autotile context returned while its desktop is out of view
# brings its split ratio back. Runner: 2 outputs, no flags (kscreen-doctor).
# Staging: two global desktops, Virtual-1 autotile:master-stack on desktop 1 and COLS3 on desktop 2,
#   Virtual-0 COLS3, Snap Assist off. t1 and t2 tiled on Virtual-1 desktop 1, increase_master_ratio
#   pressed three times with t1 active. Virtual-1 is disabled, desktop 2 shown, Virtual-1 enabled
#   (KWin returns t1 and t2 while desktop 1 is out of view), then desktop 1 shown again.
# Asserts: back on desktop 1 the master tile (the leftmost on Virtual-1) has the tuned width.
# A FAIL means: the tuning came back only on an in-view re-seat and the context returned at the
#   algorithm's default ratio.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
newdesk 1 D2
assign Virtual-0 "$COLS3"
assignd Virtual-1 1 "autotile:master-stack"
assignd Virtual-1 2 "$COLS3"
sleep 1.5
desk 1
for c in t1 t2; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 2
# masterw -> width of the tile whose x is the leftmost on Virtual-1
masterw() {
    local a b
    a=$(geo t1)
    b=$(geo t2)
    if [ "$(echo "$a" | awk '{ print int($1) }')" -le "$(echo "$b" | awk '{ print int($1) }')" ]; then
        echo "$a" | awk '{ print int($3) }'
    else
        echo "$b" | awk '{ print int($3) }'
    fi
}
W_DEF=$(masterw)
echo "staged: t1 $(geo t1) | t2 $(geo t2) | tiles $(tiles Virtual-1) | master width $W_DEF (default)"
case "$(tiles Virtual-1)" in 2*) ;; *) stage_fail "Virtual-1 does not hold two tiles" ;; esac
activate t1
for _ in 1 2 3; do
    shortcut increase_master_ratio
    sleep 0.7
done
sleep 1.5
W_TUNED=$(masterw)
echo "tuned: t1 $(geo t1) | t2 $(geo t2) | master width $W_TUNED"
[ "$W_TUNED" != "$W_DEF" ] || stage_fail "increase_master_ratio did not change the master width"
echo "== Virtual-1 disabled, desktop 2 shown, Virtual-1 enabled"
dmark
ksd output.Virtual-1.disable
sleep 4
desk 2
echo "  desktop $(curdesk)"
ksd output.Virtual-1.enable
sleep 5
echo "  readopt lines: $(dlog "AutotileEngine::readoptParked: .*" | sed 's/org.pz.plain|//g' | tr '\n' ';' | cut -c1-300)"
echo "  t1 $(geo t1) | t2 $(geo t2)"
for c in t1 t2; do
    [ "$(outof "$(geo "$c")")" = Virtual-1 ] || stage_fail "KWin did not return $c to Virtual-1 ($(geo "$c"))"
done
desk 1
sleep 1.5
echo "  desktop $(curdesk): t1 $(geo t1) | t2 $(geo t2) | tiles $(tiles Virtual-1)"
check "master width back on desktop 1 (default was $W_DEF)" "$(masterw)" = "$W_TUNED"
finish
