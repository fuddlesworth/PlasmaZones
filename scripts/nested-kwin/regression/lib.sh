#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# lib.sh: the shared library of the nested-KWin regression probes. run.sh starts a fresh
# nested session per probe and runs `bash probes/NAME.sh NEST LOG`, where NEST is the session's
# PZ_NESTED_DIR and LOG the compositor's output. A probe begins
#   set -u
#   NEST="$1"
#   LOG="$2"
#   . "$(dirname "$0")/../lib.sh"
# Sourcing waits for the effect to come up, reads the session's env.sh and defines the helpers
# below. Nothing here starts or stops a session or builds anything.
#
# Result contract. A probe asserts with check and ends with finish:
#   exit 0  PASS            every check held
#   exit 1  FAIL            a check failed, or the nested daemon died while the probe ran
#   exit 2  STAGING-FAILED  stage_fail was called, the probe ended before finish, or it ran no
#                           check: the staging did not hold, so the verdict does not count
# The last line of the output says the same: "RESULT: PASS", "RESULT: FAIL" or
# "RESULT: STAGING-FAILED", with the reason in brackets.
#
# Readers go through committed geometry (KWin scripting), the daemon's D-Bus getters, dbus-monitor
# and the two logs. Besides the test clients the library signals one process, the NESTED daemon,
# and only through the dpid gate.

PZR_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PZR_REPO="$(cd "$PZR_DIR/../../.." && pwd)"
PZR_CLIENTS="$PZR_DIR/clients"
PZR_FAKEINPUT="${PZR_FAKEINPUT:-}"
PZR_CHECKS=0
PZR_FAILS=0
PZR_DONE=0
PZR_CLEANUP=()
PZR_MONPIDS=""
# Set while a probe has stopped the daemon on purpose (dkill), so finish does not count the
# missing daemon as a crash.
PZR_DAEMON_DOWN=0
DSTOPPED=""

# --------------------------------------------------------------------------------------------
# Results
# --------------------------------------------------------------------------------------------

# on_exit 'COMMAND' adds a cleanup step that finish, stage_fail and an early exit run, in the
# order added. Restore anything a probe changed outside the session here. Call on_exit, the
# helpers that register a step (cstop, dstop, dkill, mon_start) and the marks (emark, dmark,
# mmark) directly and never inside $( ): what a subshell sets never reaches the probe's shell.
on_exit() { PZR_CLEANUP+=("$1"); }

_pzr_cleanup() {
    local c
    # dcont's body: this can run before the daemon helpers below are defined.
    [ -n "$DSTOPPED" ] && kill -CONT "$DSTOPPED"
    DSTOPPED=""
    for c in "${PZR_CLEANUP[@]}"; do
        eval "$c"
    done
    PZR_CLEANUP=()
    # shellcheck disable=SC2086
    [ -n "$PZR_MONPIDS" ] && kill $PZR_MONPIDS 2>/dev/null
    PZR_MONPIDS=""
}

_pzr_end() {
    PZR_DONE=1
    _pzr_cleanup
    echo "RESULT: $2 ($3)"
    exit "$1"
}

_pzr_trap() {
    local rc=$?
    [ "$PZR_DONE" = 1 ] && return
    _pzr_end 2 STAGING-FAILED "the probe ended before finish, status $rc"
}
trap _pzr_trap EXIT
trap 'exit 2' TERM INT HUP

# check DESC ACTUAL OP WANT: one assertion, printed as a PASS or FAIL line. OP is
#   =  !=                    string comparison
#   -eq -ne -lt -le -gt -ge  integer comparison (a non-integer ACTUAL fails)
#   ~  !~                    ACTUAL matches, or does not match, the extended regex WANT
check() {
    local desc="$1" got="$2" op="$3" want="$4" ok=1
    PZR_CHECKS=$((PZR_CHECKS + 1))
    case "$op" in
    =) [ "$got" = "$want" ] || ok=0 ;;
    !=) [ "$got" != "$want" ] || ok=0 ;;
    -eq | -ne | -lt | -le | -gt | -ge)
        if [[ "$got" =~ ^-?[0-9]+$ && "$want" =~ ^-?[0-9]+$ ]]; then
            [ "$got" "$op" "$want" ] || ok=0
        else
            ok=0
        fi
        ;;
    '~') [[ "$got" =~ $want ]] || ok=0 ;;
    '!~') [[ "$got" =~ $want ]] && ok=0 ;;
    *) stage_fail "probe bug: check '$desc' names an unknown operator '$op'" ;;
    esac
    if [ "$ok" = 1 ]; then
        echo "  PASS: $desc (got '$got')"
    else
        PZR_FAILS=$((PZR_FAILS + 1))
        echo "  FAIL: $desc: got '$got', want $op '$want'"
    fi
}

# stage_fail REASON: the staging did not hold. Ends the probe STAGING-FAILED.
stage_fail() {
    echo "STAGING FAILED: $1"
    _pzr_end 2 STAGING-FAILED "$1"
}

# note TEXT: an informational line, never a verdict.
note() { echo "  note: $*"; }

# finish: run the cleanup, count a dead daemon as a failure, and end with the verdict.
finish() {
    _pzr_cleanup
    if [ "$PZR_DAEMON_DOWN" = 0 ] && ! dpid >/dev/null; then
        PZR_CHECKS=$((PZR_CHECKS + 1))
        PZR_FAILS=$((PZR_FAILS + 1))
        echo "  FAIL: the nested daemon is gone"
    elif [ "$PZR_DAEMON_DOWN" = 0 ] && ! dlogged_wait; then
        PZR_CHECKS=$((PZR_CHECKS + 1))
        PZR_FAILS=$((PZR_FAILS + 1))
        echo "  FAIL: the nested daemon was replaced ($(downer_note)): it died and D-Bus activation started another"
    fi
    if [ "$PZR_CHECKS" = 0 ]; then
        _pzr_end 2 STAGING-FAILED "no check ran"
    elif [ "$PZR_FAILS" -gt 0 ]; then
        _pzr_end 1 FAIL "$PZR_FAILS of $PZR_CHECKS checks failed"
    fi
    _pzr_end 0 PASS "$PZR_CHECKS checks"
}

# --------------------------------------------------------------------------------------------
# Session
# --------------------------------------------------------------------------------------------

[ -n "${NEST:-}" ] && [ -n "${LOG:-}" ] || stage_fail "probe bug: NEST and LOG must be set before sourcing lib.sh"
# A previous probe's pre-restart daemon log (drestart) is not this probe's. Removed before the wait
# below, so a probe that stage-fails there does not leave it to be filed under its own name.
rm -f "$NEST/daemon.log.earlier"
# The effect logs "need snap restore" once it has its first answer from the daemon.
for _ in $(seq 120); do
    grep -q "need snap restore" "$LOG" 2>/dev/null && break
    sleep 1
done
grep -q "need snap restore" "$LOG" 2>/dev/null || stage_fail "the effect never came up (no 'need snap restore' in $LOG)"
[ -r "$NEST/env.sh" ] || stage_fail "no nested session state at $NEST/env.sh"
. "$NEST/env.sh"

# Distros ship the Qt6 D-Bus CLI under three names; probes call it qdbus6.
if ! command -v qdbus6 >/dev/null 2>&1; then
    for _c in qdbus-qt6 qdbus; do
        if command -v "$_c" >/dev/null 2>&1; then
            eval "qdbus6() { $_c \"\$@\"; }"
            break
        fi
    done
fi

COLS3="{678892d8-b63a-41a9-81b9-78caa95878e7}"
COLS2="{2bd3c71a-1609-4641-bbe2-b9c1284fcdc1}"
GRID4="{25828c9b-68f7-4256-a81b-9eaf4450a110}"

PZ() { qdbus6 org.plasmazones /PlasmaZones "$@" 2>&1; }

# kwjs 'JS BODY': run a one-off KWin script in the nested session. print() output lands in the
# compositor log as "js: ...". js is the same with the settle the readers below rely on.
kwjs() {
    local f="$NEST/kwjs-$BASHPID-$RANDOM.js"
    printf '%s\n' "$1" > "$f"
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$f" >/dev/null
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
    sleep 0.7
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$f" >/dev/null 2>&1
    rm -f "$f"
}
js() {
    kwjs "$1"
    sleep 0.4
}

# jsread TAG 'JS EXPR': print the value of EXPR from a KWin script and echo it. A fresh nonce per
# read, so a script that printed nothing reads as empty rather than as the last read's value.
jsread() {
    local nonce="$1$RANDOM$RANDOM"
    js "print(\"JSR $nonce \" + ($2));"
    grep "js: JSR $nonce " "$LOG" | tail -1 | sed "s/.*js: JSR $nonce //"
}

# geo CAPTION -> "x y w h max=N nb=B min=B d=[desktops] @output", empty when KWin lists no window
# with that caption.
geo() {
    local nonce="$RANDOM$RANDOM"
    js "const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (w[i].caption === \"$1\") { print(\"GEO $nonce \" + w[i].frameGeometry.x + \" \" + w[i].frameGeometry.y + \" \" + w[i].frameGeometry.width + \" \" + w[i].frameGeometry.height + \" max=\" + w[i].maximizeMode + \" nb=\" + w[i].noBorder + \" min=\" + w[i].minimized + \" d=[\" + w[i].desktops.map(function (d) { return d.x11DesktopNumber; }) + \"] @\" + w[i].output.name); } }"
    grep "js: GEO $nonce " "$LOG" | tail -1 | sed "s/.*js: GEO $nonce //"
}
rect() { echo "$1" | awk '{ print $1, $2, $3, $4 }'; }
size() { echo "$1" | awk '{ print $3 "x" $4 }'; }
maxof() { echo "$1" | grep -o "max=[0-9]*" | cut -d= -f2; }
nbof() { echo "$1" | grep -o "nb=[a-z]*" | cut -d= -f2; }
outof() { echo "$1" | grep -o "@[^ ]*$" | cut -c2-; }
deskof() { echo "$1" | grep -o "d=\[[0-9,]*\]" | sed 's/d=\[//; s/\]//'; }
# wprop CAPTION 'EXPR' -> EXPR evaluated on the window with CAPTION as w (w.pid, w.keepAbove ...)
wprop() {
    local nonce="$RANDOM$RANDOM"
    js "const ws = workspace.windowList(); for (var i = 0; i < ws.length; i++) { if (ws[i].caption === \"$1\") { var w = ws[i]; print(\"WPROP $nonce \" + ($2)); } }"
    grep "js: WPROP $nonce " "$LOG" | tail -1 | sed "s/.*js: WPROP $nonce //"
}
# wincount CAPTION -> how many windows KWin lists with CAPTION now
wincount() {
    local nonce="$RANDOM$RANDOM"
    js "var n = 0; const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (w[i].caption === \"$1\") { n++; } } print(\"WCOUNT $nonce \" + n);"
    grep "js: WCOUNT $nonce " "$LOG" | tail -1 | sed "s/.*js: WCOUNT $nonce //"
}
# screens -> "NAME=x,y,w,h ..." for every output
screens() {
    jsread SCR '(function () { var s = []; for (var i = 0; i < workspace.screens.length; i++) { var g = workspace.screens[i].geometry; s.push(workspace.screens[i].name + "=" + g.x + "," + g.y + "," + g.width + "," + g.height); } return s.join(" "); })()'
}
# scr NAME -> "x y w h" of output NAME from the last screens() read in $SCR
scr() { echo "$SCR" | tr ' ' '\n' | grep "^$1=" | sed "s/^$1=//" | tr ',' ' '; }
# kwjs_ok: the KWin scripting flake is a session where scripts print nothing. Call it once right
# after sourcing; it also fills $SCR. need_outputs N: stage_fail unless the session has N outputs.
kwjs_ok() {
    SCR=$(screens)
    [ -n "$SCR" ] || stage_fail "KWin scripts printed nothing (the KWin scripting flake); rerun the probe"
    echo "outputs: $SCR"
}
need_outputs() {
    local n
    n=$(echo "$SCR" | wc -w)
    [ "$n" -ge "$1" ] || stage_fail "the probe needs $1 outputs, the session has $n"
}
curdesk() { jsread CURD 'workspace.currentDesktop.x11DesktopNumber + " of " + workspace.desktops.length'; }
desk() {
    js "workspace.currentDesktop = workspace.desktops[$(($1 - 1))];"
    sleep 2
}
activecap() { jsread ACTW '(workspace.activeWindow ? workspace.activeWindow.caption : "(none)")'; }
# on CAPTION 'STATEMENT' runs STATEMENT on the window with CAPTION as w[i]
on() { js "const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (w[i].caption === \"$1\") { $2 } }"; }
# activate CAPTION [SECONDS]: focus the window, then settle SECONDS (default 0.6)
activate() {
    on "$1" "workspace.activeWindow = w[i];"
    sleep "${2:-0.6}"
}
todesk() { on "$1" "w[i].desktops = [workspace.desktops[$(($2 - 1))]];"; }
moveto() { on "$1" "w[i].frameGeometry = {x: $2, y: $3, width: w[i].frameGeometry.width, height: w[i].frameGeometry.height};"; }
# sendto_once CAPTION OUTPUT: one KWin Move to Screen of the window onto OUTPUT, unless it is there
# already, with no settle and no retry. sendto CAPTION OUTPUT: the same, retried; fails when the
# window is not on OUTPUT after three tries
sendto_once() {
    js "var o = null; for (var i = 0; i < workspace.screens.length; i++) { if (workspace.screens[i].name === \"$2\") { o = workspace.screens[i]; } } const w = workspace.windowList(); for (var j = 0; j < w.length; j++) { if (w[j].caption === \"$1\" && o && w[j].output !== o) { workspace.sendClientToScreen(w[j], o); } }"
}
sendto() {
    for _ in 1 2 3; do
        sendto_once "$1" "$2"
        sleep 2
        [ "$(outof "$(geo "$1")")" = "$2" ] && return 0
    done
    return 1
}
newdesk() {
    qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.createDesktop "$1" "$2" >/dev/null 2>&1
    sleep 1
}
# per_output_desktops: turn on KWin's per-output virtual desktops in the nested kwinrc
per_output_desktops() {
    printf '\n[Windows]\nPerOutputVirtualDesktops=true\n' >> "$XDG_CONFIG_HOME/kwinrc"
    qdbus6 org.kde.KWin /KWin reconfigure >/dev/null 2>&1
    sleep 1.5
}
ksd() {
    WAYLAND_DISPLAY="$WAYLAND_DISPLAY" kscreen-doctor "$@" > "$NEST/pzr-ksd.out" 2>&1
    echo "kscreen-doctor $* exit=$? $(head -c 140 "$NEST/pzr-ksd.out" | tr '\n' ' ')"
}

# ids: print every normal window's internalId, caption and frame into the compositor log as
# "js: IDWIN {uuid} cap=CAPTION geo=x,y wxh"; uuid CAPTION then reads the newest one.
ids() {
    kwjs 'const wins = workspace.windowList();
for (var i = 0; i < wins.length; i++) {
    var w = wins[i];
    if (!w.normalWindow) continue;
    print("IDWIN " + w.internalId + " cap=" + w.caption + " geo=" + w.frameGeometry.x + "," + w.frameGeometry.y + " "
          + w.frameGeometry.width + "x" + w.frameGeometry.height);
}'
    sleep 1.5
}
uuid() { grep "IDWIN .* cap=$1 " "$LOG" | tail -1 | sed 's/.*IDWIN {\([^}]*\)}.*/\1/'; }

# --------------------------------------------------------------------------------------------
# Test clients (clients/*.py)
# --------------------------------------------------------------------------------------------

# client SCRIPT ARG...: a test client on the nested Wayland display; xclient SCRIPT ARG...: the
# same on the nested Xwayland; qtclient SCRIPT ARG...: a PySide6 client, run without the session's
# QT_PLUGIN_PATH (it points at the build tree's plugins); xqtclient SCRIPT ARG...: the same on the
# nested Xwayland. Each waits 3 s for the window to map.
client() {
    local s="$1"
    shift
    setsid env WAYLAND_DISPLAY="$WAYLAND_DISPLAY" GDK_BACKEND=wayland python3 "$PZR_CLIENTS/$s" "$@" \
        >/dev/null 2>&1 < /dev/null &
    sleep 3
}
xclient() {
    local s="$1" xd
    shift
    xd=$(xdisplay) || stage_fail "no nested Xwayland display (the probe needs flag X)"
    setsid env DISPLAY="$xd" GDK_BACKEND=x11 python3 "$PZR_CLIENTS/$s" "$@" >/dev/null 2>&1 < /dev/null &
    sleep 3
}
qtclient() {
    local s="$1"
    shift
    setsid env -u QT_PLUGIN_PATH WAYLAND_DISPLAY="$WAYLAND_DISPLAY" QT_QPA_PLATFORM=wayland \
        python3 "$PZR_CLIENTS/$s" "$@" >/dev/null 2>&1 < /dev/null &
    sleep 3
}
xqtclient() {
    local s="$1" xd
    shift
    xd=$(xdisplay) || stage_fail "no nested Xwayland display (the probe needs flag X)"
    setsid env -u QT_PLUGIN_PATH -u WAYLAND_DISPLAY DISPLAY="$xd" QT_QPA_PLATFORM=xcb \
        python3 "$PZR_CLIENTS/$s" "$@" >/dev/null 2>&1 < /dev/null &
    sleep 3
}
# open CAPTION [W H]: plain_gtk.py (app id org.pz.plain); openapp CAPTION APPID [W H]: appid_gtk.py;
# xopen CAPTION [W H]: plain_gtk.py on Xwayland
open() { client plain_gtk.py "$1" "${2:-400}" "${3:-300}"; }
openapp() { client appid_gtk.py "$1" "$2" "${3:-400}" "${4:-300}"; }
xopen() { xclient plain_gtk.py "$1" "${2:-400}" "${3:-300}"; }
# xdisplay -> ":N", the nested Xwayland's display, status 1 when none is found in about 15 s. It is
# the first argument of the Xwayland process the nested compositor (kwinpid) started. Before that
# process exists, it is the lowest-numbered X socket that was not there when run.sh started the
# session (PZR_X11_BEFORE) and is not the host's own display: KWin opens two X sockets and serves
# the display on the lower one, so a client sent to the higher one never maps. That second reading
# needs PZR_X11_BEFORE set (run.sh sets it, empty when the host had no X socket). Without it every
# X socket on the host looks new, so with it unset only the Xwayland process is read.
xdisplay() {
    local k x d s
    for _ in $(seq 15); do
        k=$(kwinpid)
        if [ -n "$k" ]; then
            for x in $(pgrep -P "$k" -x Xwayland); do
                d=$(tr '\0' '\n' < "/proc/$x/cmdline" 2>/dev/null | sed -n 2p)
                case "$d" in :[0-9]*)
                    echo "$d"
                    return 0
                    ;;
                esac
            done
        fi
        if [ -n "${PZR_X11_BEFORE+set}" ]; then
            for s in $(ls /tmp/.X11-unix/ 2>/dev/null | sort -V); do
                d=":${s#X}"
                [ "$d" = "${PZR_HOST_DISPLAY:-}" ] && continue
                case " $PZR_X11_BEFORE " in *" $s "*) continue ;; esac
                echo "$d"
                return 0
            done
        fi
        sleep 1
    done
    return 1
}
# cpid CAPTION -> the pid of the test client showing CAPTION, gated on its command line naming a
# script under clients/ so a pid KWin reports for anything else is never signalled
cpid() {
    local p
    p=$(wprop "$1" 'w.pid')
    [ -n "$p" ] && [ "$p" != 0 ] || return 1
    tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null | grep -q "$PZR_CLIENTS/" || return 1
    echo "$p"
}
# cstop CAPTION: freeze the client (SIGSTOP) so it stops answering configures, and leave its pid in
# CPID. Call it directly, never in $( ): the cleanup that resumes it must reach this shell. ccont PID
# resumes it earlier; finish resumes it otherwise.
cstop() {
    CPID=$(cpid "$1") || stage_fail "no test client pid for $1"
    kill -STOP "$CPID"
    on_exit "kill -CONT $CPID 2>/dev/null"
}
ccont() { kill -CONT "$1" 2>/dev/null; }
# ckill CAPTION: close the client the hard way (SIGTERM to its gated pid)
ckill() {
    local p
    p=$(cpid "$1") || return 1
    kill "$p"
}

# fake CMD...: drive pointer and keyboard through the fakeinput tool (org_kde_kwin_fake_input; the
# probe needs flag F, which also turns KWin's permission checks off). See fakeinput/main.cpp.
fake() {
    [ -x "$PZR_FAKEINPUT" ] || stage_fail "no fakeinput binary (the probe needs flag F; run.sh builds it)"
    WAYLAND_DISPLAY="$WAYLAND_DISPLAY" "$PZR_FAKEINPUT" "$@"
}
# touchdrag CAPTION DX DY: a user Meta+left drag of CAPTION by (DX, DY), so KWin counts an
# interactive move
touchdrag() {
    local g x y w h cx cy
    g=$(geo "$1")
    read -r x y w h _ <<< "$g"
    cx=$((${x%.*} + ${w%.*} / 2))
    cy=$((${y%.*} + ${h%.*} / 2))
    fake abs "$cx" "$cy" wait 100 keydown 125 wait 50 down left wait 150 \
        path "$cx" "$cy" $((cx + $2)) $((cy + $3)) 10 30 wait 300 up left wait 50 keyup 125
    sleep 1.5
}
# altdrag CAPTION X Y: a Meta+left drag of CAPTION from its centre to (X, Y) with the snap trigger
# (Alt, the default) pressed once the move is under way, dropped there
altdrag() {
    local g x y w h cx cy
    g=$(geo "$1")
    [ -n "$g" ] || stage_fail "no frame for $1 to drag"
    read -r x y w h _ <<< "$g"
    cx=$((${x%.*} + ${w%.*} / 2))
    cy=$((${y%.*} + ${h%.*} / 2))
    fake abs "$cx" "$cy" wait 100 keydown 125 wait 50 down left wait 150 \
        keydown 56 wait 150 path "$cx" "$cy" "$2" "$3" 20 30 wait 900 up left wait 80 keyup 56 keyup 125
    sleep 2
}
# metaresize X Y DX DY: KWin's modifier resize, a Meta+right drag from (X, Y) by (DX, DY), then a
# 2.5 s settle. KWin picks the edges from where the press lands: the middle third of a side takes
# that edge alone, a corner third takes both of its edges.
metaresize() {
    fake abs "$1" "$2" wait 100 keydown 125 wait 50 down right wait 150 \
        path "$1" "$2" $(($1 + $3)) $(($2 + $4)) 10 30 wait 300 up right wait 50 keyup 125
    sleep 2.5
}

# --------------------------------------------------------------------------------------------
# Daemon
# --------------------------------------------------------------------------------------------

floating() { PZ org.plasmazones.WindowTracking.isWindowFloating "$1"; }
zoneof() { PZ org.plasmazones.WindowTracking.getZoneForWindow "$1"; }
# wstate ID -> gdbus's (windowId, zoneId, screenId, isFloating, changeType, zoneIds, isSticky) tuple
wstate() {
    gdbus call --session --dest org.plasmazones --object-path /PlasmaZones \
        --method org.plasmazones.WindowTracking.getWindowState "$1" 2>&1 | tr -d '\n'
}
wscreen() { wstate "$1" | grep -o "'[^']*'" | sed -n 3p | tr -d "'"; }
screenid() { PZ org.plasmazones.Screen.getScreenId "$1"; }
modeof() { PZ org.plasmazones.LayoutRegistry.getModeForScreenDesktop "$1" "$2"; }
setting() { PZ org.plasmazones.Settings.setSetting "$1" "$2" >/dev/null; }
# gsetting KEY GVARIANT: setSetting through gdbus, for values qdbus6 cannot type (<0>, <true>)
gsetting() {
    gdbus call --session --dest org.plasmazones --object-path /PlasmaZones \
        --method org.plasmazones.Settings.setSetting "$1" "$2" 2>&1
}
# gsetting_ok KEY GVARIANT: gsetting that stage-fails unless the daemon answers true, so a refused
# key never leaves the probe running on that key's default
gsetting_ok() {
    local r
    r=$(gsetting "$1" "$2")
    [ "$r" = "(true,)" ] || stage_fail "setSetting $1 $2 was not taken ($r)"
}
assign() { PZ org.plasmazones.LayoutRegistry.assignLayoutToScreen "$1" "$2" >/dev/null; }
assignd() { PZ org.plasmazones.LayoutRegistry.assignLayoutToScreenDesktop "$1" "$2" "$3" >/dev/null; }
shortcut() {
    qdbus6 org.kde.kglobalaccel /component/plasmazonesd org.kde.kglobalaccel.Component.invokeShortcut "$1" \
        >/dev/null 2>&1
}
yesno() { if eval "$1"; then echo yes; else echo no; fi; }
# within GOT WANT [TOL] -> "yes" when the integer GOT is within TOL (default 2) of WANT, else
# "no (GOT vs WANT)"; assert it with check DESC "$(within ...)" = yes
within() {
    # A failed read hands an empty or non-numeric value, which arithmetic
    # would take for 0 and so answer yes beside a want of 0. Each argument
    # must be a whole integer, as check's numeric compares require.
    local t="${3:-2}"
    if ! [[ "$1" =~ ^-?[0-9]+$ && "$2" =~ ^-?[0-9]+$ && "$t" =~ ^[0-9]+$ ]]; then
        echo "no ($1 vs $2)"
        return
    fi
    local d=$(($1 - $2))
    if [ "$d" -ge "-$t" ] && [ "$d" -le "$t" ]; then echo yes; else echo "no ($1 vs $2)"; fi
}
# zoneid LAYOUT N -> the id of zone number N in layout LAYOUT
zoneid() {
    PZ org.plasmazones.LayoutRegistry.getLayout "$1" | python3 -c '
import json, sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit()
for z in d.get("zones", []):
    if z.get("zoneNumber") == int(sys.argv[1]):
        print(z.get("id"))' "$2"
}
# tiles SCREEN -> "N [id-tails]" from Tiling.currentTilesJson (N = entries not floating)
tiles() {
    PZ org.plasmazones.Tiling.currentTilesJson "$1" | python3 -c '
import json, sys
try:
    a = json.load(sys.stdin)
except Exception as e:
    print("unparsable:", e); sys.exit()
print(len([e for e in a if not e.get("floating")]), sorted(e.get("windowId", "")[-8:] + ("(float)" if e.get("floating") else "") for e in a))'
}
# tileorder SCREEN -> Tiling.currentTilesJson's entries in order as captions ("(float)" marks a
# floating entry), named from the IDWIN lines ids left in the log. The list is the last batch the
# daemon sent for SCREEN, whatever desktop it was for: read it only after a batch you counted.
tileorder() {
    PZ org.plasmazones.Tiling.currentTilesJson "$1" | python3 -c '
import json, re, sys
log = open(sys.argv[1], errors="replace").read()
names = dict(re.findall(r"IDWIN \{([0-9a-f-]+)\} cap=(\S+)", log))
try:
    a = json.load(sys.stdin)
except Exception as e:
    print("unparsable:", e); sys.exit()
out = []
for e in a:
    u = e.get("windowId", "").split("|")[-1].strip("{}")
    out.append(names.get(u, u[-8:]) + ("(float)" if e.get("floating") else ""))
print(" ".join(out) if out else "(none)")' "$LOG"
}
# striporder SCREEN -> the strip's columns in order as captions ("a | b | f+c" for a two-tile
# column), from Scrolling.stripModelJson and the IDWIN lines (run ids first)
striporder() {
    PZ org.plasmazones.Scrolling.stripModelJson "$1" | python3 -c '
import json, re, sys
log = open(sys.argv[1], errors="replace").read()
names = dict(re.findall(r"IDWIN \{([0-9a-f-]+)\} cap=(\S+)", log))
try:
    m = json.load(sys.stdin)
except Exception as e:
    print("unparsable:", e); sys.exit()
cols = []
for c in m.get("columns", []):
    ts = []
    for t in c.get("tiles", []):
        u = t.get("windowId", "").split("|")[-1].strip("{}")
        ts.append(names.get(u, u[-8:]))
    cols.append("+".join(ts))
print(" | ".join(cols) if cols else "(empty)")' "$LOG"
}
# instrip CAPTION [SCREEN] -> "yes" when a column of SCREEN's strip (default Virtual-0) holds
# CAPTION, else "no" (run ids first, so striporder can name it)
instrip() {
    if striporder "${2:-Virtual-0}" | tr ' |+' '\n\n\n' | grep -qxF "$1"; then echo yes; else echo no; fi
}

# dpid -> the nested daemon's pid, or nothing (status 1): the process that owns org.plasmazones
# on the nested bus (the bus daemon's own answer, whichever way the daemon was started), gated two
# more ways so it can never name the host daemon: its comm is plasmazonesd, and its environment
# carries a dbus-run-session bus under /tmp/dbus- (the login bus never does).
dpid() {
    local p=""
    # An empty answer is retried for about 2 s: the name has no owner for a moment while a daemon
    # hands over, and a bus call can fail once without the daemon being gone.
    for _ in 1 2 3 4 5; do
        p=$(dbus-send --session --print-reply --dest=org.freedesktop.DBus / \
            org.freedesktop.DBus.GetConnectionUnixProcessID string:org.plasmazones 2>/dev/null | awk '/uint32/ { print $2 }')
        [ -n "$p" ] && break
        sleep 0.4
    done
    [ -n "$p" ] || return 1
    [ "$(cat "/proc/$p/comm" 2>/dev/null)" = "plasmazonesd" ] || return 1
    tr '\0' '\n' < "/proc/$p/environ" 2>/dev/null | grep -q '^DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-' || return 1
    echo "$p"
}
# dlogged: the running daemon is the one daemon.sh started, so daemon.log is its log. A daemon the
# effect D-Bus-activated instead logs into the compositor's output, and reads through dlog would
# miss every line. run.sh keeps activation blocked for the whole session to rule that out; this
# is the check that it held.
dlogged() {
    local p
    p=$(dpid) || return 1
    [ "$p" = "$(cat "$NEST/daemon.pid" 2>/dev/null)" ]
}
# dlogged_wait: dlogged, retried for about 5 s, for the checks that run right after a start or a
# restart, while a daemon that lost the name race may still be on its way out
dlogged_wait() {
    for _ in $(seq 10); do
        dlogged && return 0
        sleep 0.5
    done
    return 1
}
# downer_note -> "owner pid P, daemon.pid D" for a failure message ("none" for a missing one)
downer_note() { echo "owner pid $(dpid || echo none), daemon.pid $(cat "$NEST/daemon.pid" 2>/dev/null || echo none)"; }
# dstop: freeze the nested daemon (SIGSTOP; its bus traffic queues at the bus), status 1 when the
# gate finds no daemon. dcont lets it run again; every cleanup runs dcont first, so a failed
# staging never leaves the daemon stopped.
dstop() {
    DSTOPPED=$(dpid) || {
        DSTOPPED=""
        echo "  no gated nested daemon pid, the daemon was not stopped"
        return 1
    }
    kill -STOP "$DSTOPPED"
}
dcont() {
    [ -n "$DSTOPPED" ] && kill -CONT "$DSTOPPED"
    DSTOPPED=""
}
# dsend METHOD ARGS...: a method call that queues behind a frozen daemon. dbus-send with no
# --print-reply sends at once and waits for nothing; gdbus call would introspect the destination
# first and send the real call only after dcont.
# Example: dsend org.plasmazones.Snap.windowUnsnapped "string:$ID"
dsend() {
    local method="$1"
    shift
    dbus-send --session --type=method_call --dest=org.plasmazones /PlasmaZones "$method" "$@"
}
_pzr_block_activation() {
    [ -f "$NEST/activate-daemon.sh.pzr" ] || cp "$NEST/activate-daemon.sh" "$NEST/activate-daemon.sh.pzr"
    printf '#!/bin/sh\nexit 1\n' > "$NEST/activate-daemon.sh"
}
_pzr_restore_activation() {
    [ -f "$NEST/activate-daemon.sh.pzr" ] && mv -f "$NEST/activate-daemon.sh.pzr" "$NEST/activate-daemon.sh"
}
# dkill: stop the nested daemon (SIGTERM through the dpid gate) and block D-Bus activation, so the
# effect's next call does not start another one (under run.sh it is blocked already). drestart
# starts it again with the repo's daemon.sh while activation stays blocked, so no activated
# daemon can take the name between daemon.sh's eviction and its own start; daemon.sh truncates
# daemon.log, so drestart first appends the old log to daemon.log.earlier (run.sh keeps that file
# too). dmark again after a restart. finish restores activation and does not count a daemon
# stopped this way as a crash.
dkill() {
    local p
    p=$(dpid) || stage_fail "no gated nested daemon pid, the daemon was not stopped"
    _pzr_block_activation
    on_exit _pzr_restore_activation
    PZR_DAEMON_DOWN=1
    kill "$p"
    for _ in $(seq 50); do
        [ -d "/proc/$p" ] || return 0
        sleep 0.2
    done
    stage_fail "the nested daemon $p did not exit on SIGTERM"
}
drestart() {
    _pzr_block_activation
    on_exit _pzr_restore_activation
    cat "$NEST/daemon.log" >> "$NEST/daemon.log.earlier" 2>/dev/null
    PZ_NESTED_DIR="$NEST" "$PZR_REPO/scripts/nested-kwin/daemon.sh" 2>&1 | tail -1
    dpid >/dev/null || stage_fail "the nested daemon did not come back"
    dlogged_wait || stage_fail "the daemon owning org.plasmazones is not the one daemon.sh started ($(downer_note))"
    PZR_DAEMON_DOWN=0
}

# --------------------------------------------------------------------------------------------
# Logs and the bus
# --------------------------------------------------------------------------------------------

# emark / dmark set a mark in the compositor log / the daemon log; elog / dlog PATTERN print the
# matching text since the mark. Pass bare uuids to them: a full "app|uuid" id is an ERE alternation.
emark() { EMARK=$(($(wc -l < "$LOG") + 1)); }
dmark() { DMARK=$(($(wc -l < "$NEST/daemon.log") + 1)); }
elog() { tail -n +"${EMARK:-1}" "$LOG" | grep -oE "($1).*" | cut -c1-220; }
dlog() { tail -n +"${DMARK:-1}" "$NEST/daemon.log" | grep -oE "($1).*" | cut -c1-220; }
# settlerows UUID: the daemon's reportOutputSettle rows for UUID since dmark; verdictof UUID: the
# last one's verdict (1 Readopt, 2 EvacueeFloat, 3 Reassert, 4 UserMove)
settlerows() { dlog "reportOutputSettle: [^ ]*$1.*" | sed 's/org.pz.plain|//; s/^/    /'; }
verdictof() { dlog "reportOutputSettle: [^ ]*$1.*" | tail -1 | grep -o "verdict [0-9]*" | grep -o "[0-9]*"; }
# floatsends UUID [true|false]: setWindowFloatingForScreen sends for UUID since dmark with that value
floatsends() { dlog "setWindowFloatingForScreen: windowId= [^ ]*$1" | grep -c "floating= ${2:-true}"; }
# reseated ENGINE UUID: "<ENGINE>Engine::readoptParked: <id> re-seated" lines since dmark
reseated() { dlog "${1}Engine::readoptParked: [^ ]*$2[^ ]* re-seated" | wc -l; }
# handoffs UUID: "SnapEngine::handoffReceive: <id> ..." lines since dmark
handoffs() { dlog "SnapEngine::handoffReceive: [^ ]*$1.*" | sed 's/org.pz.plain|//'; }
# mon_start FILE MATCH...: a session dbus-monitor into FILE, stopped by finish. mmark FILE sets
# MMARK past its end; msgs FILE prints one line per message since MMARK: "member arg1 arg2 ..."
mon_start() {
    local f="$1"
    shift
    dbus-monitor --session "$@" > "$f" 2>&1 &
    PZR_MONPIDS="$PZR_MONPIDS $!"
    sleep 1
}
mmark() { MMARK=$(($(wc -l < "$1") + 1)); }
msgs() {
    tail -n +"${MMARK:-1}" "$1" | awk '
        /^(method call|signal|method return|error)/ {
            if (m != "") print m a
            m = ""; a = ""
            if (match($0, /member=[A-Za-z]+/)) m = substr($0, RSTART + 7, RLENGTH - 7)
            next
        }
        m != "" { s = $0; sub(/^ +/, "", s); sub(/^(string|int32|uint32|boolean|double) /, "", s); gsub(/"/, "", s); a = a " " s }
        END { if (m != "") print m a }'
}
# named FILE MEMBER UUID: how many MEMBER messages since MMARK name UUID
named() { msgs "$1" | grep "^$2 " | grep -c "$3"; }
# wfc FILE UUID: the floating values of the windowFloatingChanged signals naming UUID since MMARK,
# in order ("true false ..."); wfct FILE UUID: how many of them say true
wfc() { msgs "$1" | awk -v u="$2" '$1 == "windowFloatingChanged" && index($2, u) { printf "%s ", $3 }'; }
wfct() { msgs "$1" | awk -v u="$2" '$1 == "windowFloatingChanged" && index($2, u) && $3 == "true" { n++ } END { print n + 0 }'; }
# wfclast FILE UUID -> the floating value of the last windowFloatingChanged naming UUID since
# MMARK, empty when none came
wfclast() { wfc "$1" "$2" | awk '{ print $NF }'; }
# floatlisted ID -> how many entries of WindowTracking.getFloatingWindows name ID (0 or 1)
floatlisted() { PZ org.plasmazones.WindowTracking.getFloatingWindows | grep -c -F "${1##*|}"; }
# kwinpid -> the nested compositor's pid: the kwin_wayland serving this session's socket
kwinpid() { pgrep -u "$(id -u)" -f "^kwin_wayland .*--socket $WAYLAND_DISPLAY( |\$)" | head -1; }
# monocleof UUID -> the monocle flag ("true"/"false") of the last "Autotile tile request" naming
# UUID since emark
monocleof() { elog "Autotile tile request: [^ ]*$1.*" | tail -1 | grep -o "monocle= [a-z]*" | cut -d' ' -f2; }

# --------------------------------------------------------------------------------------------
# Window frames, desktops, strips, rules, session state and animation legs
# --------------------------------------------------------------------------------------------

# frame CAPTION [EXTRA] -> "x,y wxh @OUTPUT max=N" for the window with CAPTION, followed by the
# value of the JS expression EXTRA on w when given (for example '" fs=" + w.fullScreen')
frame() {
    wprop "$1" 'w.frameGeometry.x + "," + w.frameGeometry.y + " " + w.frameGeometry.width + "x" + w.frameGeometry.height + " @" + w.output.name + " max=" + w.maximizeMode'"${2:+ + ($2)}"
}
# frames CAPTION... -> "CAP x,y wxh | ..." for those windows, sorted by caption
frames() {
    local list="" c
    for c in "$@"; do
        list="$list${list:+, }\"$c\""
    done
    jsread FRAMES "(function () { var want = [$list]; var out = []; const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { var c = w[i].caption; if (want.indexOf(c) >= 0) { out.push(c + \" \" + w[i].frameGeometry.x + \",\" + w[i].frameGeometry.y + \" \" + w[i].frameGeometry.width + \"x\" + w[i].frameGeometry.height); } } out.sort(); return out.join(\" | \"); })()"
}
# wstat CAPTION ID -> "<geo> floating=B zone='ZONE'": KWin's view and the daemon's of one window
wstat() { echo "$(geo "$1") floating=$(floating "$2") zone='$(zoneof "$2")'"; }
# centre CAPTION -> "X Y", the centre of the window's frame
centre() { geo "$1" | awk '{ print int($1 + $3 / 2), int($2 + $4 / 2) }'; }
# place CAPTION X Y: move the window to (X, Y) at its size, then settle 1 s
place() {
    moveto "$1" "$2" "$3"
    sleep 1
}
# floatkey CAPTION: focus the window and press the float shortcut
floatkey() {
    activate "$1"
    shortcut toggle_window_float
    sleep 2
}
# tilelist SCREEN -> the id tails of Tiling.currentTilesJson (tiles without its count)
tilelist() { tiles "$1" | cut -d' ' -f2-; }
# layoutof SCREEN DESKTOP -> the layout assigned to SCREEN on DESKTOP
layoutof() { PZ org.plasmazones.LayoutRegistry.getLayoutForScreenDesktop "$1" "$2"; }

# deskids -> KWin's desktop ids, one per line in desktop order; deskid N -> the id of desktop N
deskids() {
    qdbus6 --literal org.kde.KWin /VirtualDesktopManager org.freedesktop.DBus.Properties.Get \
        org.kde.KWin.VirtualDesktopManager desktops | grep -o '"[0-9a-f-]\{36\}"' | tr -d '"'
}
deskid() { deskids | sed -n "${1}p"; }

# stripx [SCREEN] -> the strip's columns as "[captions extentPx] ..." ("(no strip)" when empty),
# named from the IDWIN lines ids left in the log; SCREEN defaults to Virtual-0. extent CAPTION
# [SCREEN] -> the extent of the column holding CAPTION alone, empty when it is not in the strip.
stripx() {
    PZ org.plasmazones.Scrolling.stripModelJson "${1:-Virtual-0}" | python3 -c '
import json, re, sys
names = dict(re.findall(r"IDWIN \{([0-9a-f-]+)\} cap=(\S+)", open(sys.argv[1], errors="replace").read()))
try:
    m = json.load(sys.stdin)
except Exception as e:
    print("unparsable:", e); sys.exit()
out = []
for c in m.get("columns", []):
    caps = []
    for t in c.get("tiles", []):
        u = t.get("windowId", "").split("|")[-1].strip("{}")
        caps.append(names.get(u, u[-8:]))
    out.append("[%s %s]" % ("+".join(caps), c.get("extentPx")))
print(" ".join(out) or "(no strip)")' "$LOG"
}
extent() { stripx "${2:-Virtual-0}" | grep -o "\[$1 [0-9]*\]" | head -1 | tr -d '[]' | awk '{ print $2 }'; }

# The nested rules.json, read straight from disk. rule_rows -> one "name|enabled|desktops|routes"
# line per rule that names a desktop, where desktops are its virtualDesktop match values and routes
# its routeToDesktop targets (both comma-joined); rule_row NAME -> that rule's line without the
# name; rule_desktops -> every virtualDesktop match value of every rule, sorted, comma-joined.
rule_rows() {
    python3 -c '
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception as e:
    print("unreadable|%s||" % e); sys.exit()
rules = d.get("rules", []) if isinstance(d, dict) else d
def leaves(m, out):
    if isinstance(m, dict):
        if m.get("field") == "virtualDesktop":
            out.append(m.get("value"))
        for v in m.values():
            leaves(v, out)
    elif isinstance(m, list):
        for v in m:
            leaves(v, out)
for r in rules:
    vd = []
    leaves(r.get("match", {}), vd)
    route = [a.get("targetDesktop") for a in r.get("actions", []) if a.get("type") == "routeToDesktop"]
    if vd or route:
        print("%s|%s|%s|%s" % (r.get("name", "?"), r.get("enabled"), ",".join(map(str, vd)), ",".join(map(str, route))))
' "$XDG_CONFIG_HOME/plasmazones/rules.json"
}
rule_row() { rule_rows | grep "^$1|" | cut -d'|' -f2-; }
rule_desktops() {
    python3 -c '
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception as e:
    print("unreadable", e); sys.exit()
out = []
def leaves(m):
    if isinstance(m, dict):
        if m.get("field") == "virtualDesktop":
            out.append(m.get("value"))
        for v in m.values():
            leaves(v)
    elif isinstance(m, list):
        for v in m:
            leaves(v)
for r in (d.get("rules", []) if isinstance(d, dict) else d):
    leaves(r.get("match", {}))
print(",".join(str(x) for x in sorted(out, key=str)))
' "$XDG_CONFIG_HOME/plasmazones/rules.json"
}
# rule_state ID -> "present enabled=B desktop=[values]" for the rule with ID as the daemon's
# Rules.getAllRules lists it, or "absent"
rule_state() {
    PZ org.plasmazones.Rules.getAllRules | python3 -c '
import json, sys
rid = sys.argv[1]
try:
    d = json.load(sys.stdin)
except Exception as e:
    print("unparsable:", e); sys.exit()
for r in d.get("rules", []):
    if r.get("id") == rid:
        vals = []
        def walk(m):
            if isinstance(m, dict):
                if m.get("field") == "virtualDesktop":
                    vals.append(m.get("value"))
                for v in m.values():
                    walk(v)
            elif isinstance(m, list):
                for v in m:
                    walk(v)
        walk(r.get("match"))
        print("present enabled=%s desktop=%s" % (r.get("enabled"), vals)); sys.exit()
print("absent")' "$1"
}

# placements [UID] -> the session.json WindowPlacements as "_version=V (string|object) | <id-tail>
# screen=S freeGeo={OUTPUT: x,y wxh, ...} | ...", limited to records whose windowId holds UID
placements() {
    python3 -c '
import json, sys
path = sys.argv[1]
uid = sys.argv[2] if len(sys.argv) > 2 else ""
try:
    d = json.load(open(path))
except Exception as e:
    print("session.json unreadable:", e); sys.exit()
def find(o):
    if isinstance(o, dict):
        for k, v in o.items():
            if k == "WindowPlacements":
                return v
            r = find(v)
            if r is not None:
                return r
    return None
wp = find(d)
if wp is None:
    print("(no WindowPlacements)"); sys.exit()
form = "string" if isinstance(wp, str) else "object"
if isinstance(wp, str):
    wp = json.loads(wp)
out = []
for app, recs in wp.items():
    if not isinstance(recs, list):
        continue
    for r in recs:
        if uid not in r.get("windowId", ""):
            continue
        fg = r.get("freeGeo", {})
        out.append("%s screen=%s freeGeo={%s}" % (r.get("windowId", "")[-8:], r.get("screenId"), ", ".join("%s: %s,%s %sx%s" % (k, v.get("x"), v.get("y"), v.get("width"), v.get("height")) for k, v in fg.items())))
print("_version=%s (%s) | %s" % (wp.get("_version", "absent"), form, " | ".join(out) or "(no record)"))
' "$XDG_CONFIG_HOME/plasmazones/session.json" "$@"
}

# Animation legs, from the effect's "Started animation from QRectF(..) to QRectF(..) duration: N"
# lines since emark. legs -> one "x y w h|x y w h|ms" line per leg (from, to, duration);
# legtoward "X Y W H" -> the first leg whose target is within 2 px of that rect; lastlegms -> the
# duration of the last leg. firstleg waits up to 1.5 s for the first leg and prints it raw as
# "QRectF(..)|QRectF(..)|ms", for matching against the effect's other animation lines, and
# qrectf_centre "QRectF(x,y wxh)" -> "cx cy" reads one of those rects.
legs() {
    elog "Started animation from QRectF[(][^)]*[)] to QRectF[(][^)]*[)] duration: [0-9]*" \
        | sed 's/Started animation from QRectF(\([-0-9.]*\),\([-0-9.]*\) \([0-9.]*\)x\([0-9.]*\)) to QRectF(\([-0-9.]*\),\([-0-9.]*\) \([0-9.]*\)x\([0-9.]*\)) duration: \([0-9]*\).*/\1 \2 \3 \4|\5 \6 \7 \8|\9/'
}
legtoward() {
    legs | awk -F'|' -v want="$1" '
        function near(a, b,   i, n, m, x, y) { n = split(a, x, " "); m = split(b, y, " "); if (n < 4 || m < 4) return 0;
            for (i = 1; i <= 4; i++) { if (x[i] - y[i] > 2 || y[i] - x[i] > 2) return 0 } return 1 }
        near($2, want) { print; exit }'
}
lastlegms() { elog "Started animation from .*" | tail -1 | grep -o "duration: [0-9]*" | grep -o "[0-9]*"; }
firstleg() {
    local line=""
    for _ in $(seq 30); do
        line=$(elog "Started animation from " | head -1)
        [ -n "$line" ] && break
        sleep 0.05
    done
    [ -n "$line" ] || return 1
    echo "$line" | sed 's/Started animation from \(QRectF([^)]*)\) to \(QRectF([^)]*)\) duration: \([0-9]*\).*/\1|\2|\3/'
}
qrectf_centre() {
    echo "$1" | sed 's/QRectF(\([-0-9.]*\),\([-0-9.]*\) \([0-9.]*\)x\([0-9.]*\))/\1 \2 \3 \4/' \
        | awk '{ print int($1 + $3 / 2), int($2 + $4 / 2) }'
}

# --------------------------------------------------------------------------------------------
# The last session check, here because it needs dpid: every reader of the daemon log assumes the
# running daemon is the one daemon.sh started.
# --------------------------------------------------------------------------------------------
dlogged_wait || stage_fail "the daemon owning org.plasmazones is not the one daemon.sh started ($(downer_note)), so daemon.log is not its log"
