#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# run.sh: run nested-KWin regression probes, each in a fresh nested session.
#
# Usage:
#   scripts/nested-kwin/regression/run.sh [--out DIR] [--build DIR] PROBE... | all
#   scripts/nested-kwin/regression/run.sh --list
#
#   --out DIR    where the logs go (default $XDG_RUNTIME_DIR/pz-regression/<date>-<time>)
#   --build DIR  the configure dir under test, relative to the repo root (default build, or
#                PZ_NESTED_BUILD)
#   --list       print the probe table and exit
#
# Per probe, DIR gets NAME.out (the probe's output, ending in its RESULT line), NAME.kwin.log
# (the compositor), NAME.daemon.log (plus NAME.daemon-earlier.log when the probe restarted the
# daemon) and NAME.session.log (the harness). summary.txt holds the table printed at the end.
# Exit status: 0 every probe passed, 1 a probe failed, 2 none failed but one or more could not
# stage.
#
# The sessions run on their own socket and state dir (PZ_NESTED_SOCKET, default pzregress, and
# PZ_NESTED_DIR, default $XDG_RUNTIME_DIR/pz-regress-nest), so an interactive nested session on
# the default socket is left alone. PZR_SETTLE (seconds, default 15) is the pause between one
# session's end and the next one's start.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"

# NAME SCRIPT OUTPUTS FLAGS [after=NAME] [kenv=KEY=VALUE ...] [KEY=VALUE ...]
# FLAGS: X Xwayland, N KWin's permission checks off, F the fakeinput tool (implies N), S every
# output 1600x900, K keep the previous probe's state home (with after=NAME, which run.sh runs
# immediately before), - none. kenv=KEY=VALUE goes into the nested compositor's environment
# (the effect reads it there), a plain KEY=VALUE into the probe's.
TABLE='
f71-strip-counter-own-move f71-strip-counter-own-move.sh 2 XS
f105-split-keeps-desktop-zones f105-split-keeps-desktop-zones.sh 1 -
f160-rule-after-daemon-move f160-rule-after-daemon-move.sh 2 N
f161-move-to-screen-redesk f161-move-to-screen-redesk.sh 2 N
f310-overflow-float-half f310-overflow-float-half.sh 1 S
f345-caption-push f345-caption-push.sh 1 -
f379-tile-move-rule-verdict f379-tile-move-rule-verdict.sh 2 S
f488-tiled-not-snapped f488-tiled-not-snapped.sh 1 F
f552-copied-layout-zone-ids f552-copied-layout-zone-ids.sh 1 S
f573-no-dbus-errors-daemon-gone f573-no-dbus-errors-daemon-gone.sh 2 F
f589-leg-ends-at-grab f589-leg-ends-at-grab.sh 1 FS
f593-tiling-float-refloat f593-tiling-float-refloat.sh 1 F
f622-drag-redesk-held f622-drag-redesk-held.sh 2 F
f622-drag-desktop-switch f622-drag-desktop-switch.sh 1 F
f625-restore-cache-refetch f625-restore-cache-refetch.sh 2 F
f625-rules-burst-refetch f625-rules-burst-refetch.sh 1 -
f639-floating-evacuee-unplug f639-floating-evacuee-unplug.sh 2 -
f648-stash-forgets-departed f648-stash-forgets-departed.sh 2 -
f653-cross-screen-leg-rule f653-cross-screen-leg-rule.sh 2 -
f670-snap-float-evacuee f670-snap-float-evacuee.sh 2 -
f677-plain-drop-floats-autotile f677-plain-drop-floats-autotile.sh 2 F
f678-bus-float-engine f678-bus-float-engine.sh 2 -
f680-refused-maximize-counter f680-refused-maximize-counter.sh 1 X
f685-evacuee-monocle-unmaximize f685-evacuee-monocle-unmaximize.sh 2 -
f699-tab-color-query f699-tab-color-query.sh 1 -
f702-x11-alt-release-grab f702-x11-alt-release-grab.sh 1 XF
f703-restore-cache-setting f703-restore-cache-setting.sh 1 -
f713-drag-onto-tiling-floats f713-drag-onto-tiling-floats.sh 2 F
f718-arrival-move-back-x11 f718-arrival-move-back.sh 2 XFS PZR_F718_CLIENT=x11
f718-arrival-move-back-wayland f718-arrival-move-back.sh 2 FS PZR_F718_CLIENT=wayland
f731-drag-between-tiling-monitors f731-drag-between-tiling-monitors.sh 2 F
f736-virtual-screen-drag-tracked f736-virtual-screen-drag-tracked.sh 1 FS
f738-strip-move-to-screen f738-strip-move-to-screen.sh 2 XF
f739-restart-keeps-records f739-restart-keeps-records.sh 1 -
f751-crossing-after-move-end f751-crossing-after-move-end.sh 2 F
f752-keyboard-move-leaves-zone f752-keyboard-move-leaves-zone.sh 1 F
f762-desktop-move-focus f762-desktop-move-focus.sh 1 N
f763-registry-follows-removal f763-registry-follows-removal.sh 1 -
f763-snap-tile-follow-desktops f763-snap-tile-follow-desktops.sh 1 S
f773-verbs-skip-dragged f773-verbs-skip-dragged.sh 1 F
f780-hidden-float-back f780-hidden-float-back.sh 1 S
f786-desktop-removal-renumber f786-desktop-removal-renumber.sh 1 -
f787-strips-follow-desktops f787-strips-follow-desktops.sh 1 -
f787-stashed-strip-follows-insert f787-stashed-strip-follows-insert.sh 1 -
f794-scroll-ffm f794-scroll-ffm.sh 1 FS
f798-desktop-renumber-cache f798-desktop-renumber-cache.sh 1 -
f800-removed-desktop-retile f800-removed-desktop-retile.sh 1 -
f809-removal-while-down f809-removal-while-down.sh 1 -
f811-autotile-drag-return f811-autotile-drag-return.sh 2 F
f814-spots-follow-output f814-spots-follow-output.sh 2 S
f814-login-relative-spot f814-login-relative-spot.sh 2 SFK after=f814-spots-follow-output
f814-v1-migration-rescale f814-v1-migration-rescale.sh 2 S
f815-output-move-no-reassert f815-output-move-no-reassert.sh 2 XS
f884-drop-beats-late-restore f884-drop-beats-late-restore.sh 1 F
f884-reopen-restores-zone f884-reopen-restores-zone.sh 1 F
f911-min-size-rereported f911-min-size-rereported.sh 1 XS
f923-keep-above-frame f923-keep-above-frame.sh 1 FS
f963-minimized-reslot f963-minimized-reslot.sh 1 S
f973-strip-fullscreen-maximize f973-strip-fullscreen-maximize.sh 1 X
f989-strip-maximize-legs f989-strip-maximize-legs.sh 1 X
f991-late-tileable-adopt f991-late-tileable-adopt.sh 1 -
f1009-move-inside-settle f1009-move-inside-settle.sh 3 -
f1011-monocle-arrival-unmaximize-wayland f1011-monocle-arrival-unmaximize.sh 2 -
f1011-monocle-arrival-unmaximize-x11 f1011-monocle-arrival-unmaximize.sh 2 X PZ_F1011_CLIENT=x11
f1012-disable-rule-renumber f1012-disable-rule-renumber.sh 1 -
f1013-superseded-restore-ack f1013-superseded-restore-ack.sh 2 -
f1014-escape-monocle-drag f1014-escape-monocle-drag.sh 1 F
f1015-cancelled-drag-retile-monocle f1015-cancelled-drag-retile.sh 1 F PZ_F1015_RUN=monocle
f1015-cancelled-drag-retile-bsp f1015-cancelled-drag-retile.sh 1 F PZ_F1015_RUN=bsp
f1015-cancelled-drag-retile-scroll f1015-cancelled-drag-retile.sh 1 F PZ_F1015_RUN=scroll
f1017-parked-snap-gates-disabled f1017-parked-snap-gates.sh 2 - PZ_F1017_RUN=disabled
f1017-parked-snap-gates-control f1017-parked-snap-gates.sh 2 - PZ_F1017_RUN=control
f1018-carry-not-user-snap f1018-carry-not-user-snap.sh 1 -
f1022-new-desktop-move f1022-new-desktop-move.sh 1 -
f1028-parked-strip-float-inview f1028-parked-strip-float.sh 2 - PZ_F1028_RUN=inview
f1028-parked-strip-float-hidden f1028-parked-strip-float.sh 2 - PZ_F1028_RUN=hidden
f1029-parked-overflow-float f1029-parked-overflow-float.sh 2 -
f1030-parked-ratio-returns f1030-parked-ratio-returns.sh 2 -
f1032-parked-script-bag f1032-parked-script-bag.sh 2 F
f1035-deleted-layout-minimized f1035-deleted-layout-minimized.sh 1 -
f1041-cancelled-cross-drag f1041-cancelled-cross-drag.sh 2 F
f1042-autotile-off-no-resnap f1042-autotile-off-no-resnap.sh 1 -
f1046-minimized-evacuee-tile f1046-minimized-evacuee.sh 2 - PZ_F1046_RUN=tile
f1046-minimized-evacuee-snap f1046-minimized-evacuee.sh 2 - PZ_F1046_RUN=snap
f1048-bus-float-context f1048-bus-float-context.sh 1 -
f1056-stale-ack-move f1056-stale-ack-move.sh 1 -
f1057-vmax-split-move f1057-vmax-split-move.sh 1 F
f1058-cancelled-suspended-tile f1058-cancelled-suspended-tile.sh 2 F
f1060-restatement-keeps-maximize f1060-restatement-keeps-maximize.sh 1 -
f1064-axis-fullscreen-restore-rect f1064-axis-fullscreen-restore-rect.sh 1 -
f1071-grab-released-daemon-gone f1071-grab-released-daemon-gone.sh 1 F
f1072-edge-maximize-not-cancel f1072-edge-maximize-not-cancel.sh 2 F
f1078-fullscreen-monocle-leaves f1078-fullscreen-monocle-leaves.sh 1 -
f1092-desktop-rule-renumber f1092-desktop-rule-renumber.sh 1 -
f1093-context-layout-delete f1093-context-layout-delete.sh 1 -
f1097-hidden-strip-order f1097-hidden-strip-order.sh 2 F
f1099-hidden-return-seats-tiles f1099-hidden-return-seats-tiles.sh 2 -
f1101-desktop-names-relay f1101-desktop-names-relay.sh 1 -
f1102-park-mode-switched-scroll f1102-park-mode-switched.sh 2 - PZ_F1102_RUN=scroll
f1102-park-mode-switched-tile f1102-park-mode-switched.sh 2 - PZ_F1102_RUN=tile
f1105-hidden-evacuee-mode-tile f1105-hidden-evacuee-mode.sh 2 - PZ_F1105_RUN=tile
f1105-hidden-evacuee-mode-snap f1105-hidden-evacuee-mode.sh 2 - PZ_F1105_RUN=snap
f1108-bus-float-hidden-refused f1108-bus-float-hidden-refused.sh 1 -
f1112-drag-off-removed-output-snap f1112-drag-off-removed-output.sh 2 F PZ_F1112_B=snap
f1112-drag-off-removed-output-autotile f1112-drag-off-removed-output.sh 2 F PZ_F1112_B=autotile
f1114-fullscreen-no-float-back-plain f1114-fullscreen-no-float-back.sh 1 - PZ_F1114_CLIENT=plain
f1114-fullscreen-no-float-back-fixed f1114-fullscreen-no-float-back.sh 1 - PZ_F1114_CLIENT=fixed
f1121-background-float-not-announced f1121-background-float-not-announced.sh 2 -
f1122-tiling-off-snap-float f1122-tiling-off-snap-float.sh 2 -
f1123-bus-float-other-screen f1123-bus-float-other-screen.sh 2 -
f1129-held-drag-unanswered-drop f1129-held-drag-unanswered-drop.sh 3 F
f1130-stale-unsnap-reaches-fallback f1130-stale-unsnap-reaches-fallback.sh 1 -
f1134-hidden-float-lens f1134-hidden-float-lens.sh 2 -
f1137-parked-running-mode-snap f1137-parked-running-mode.sh 2 - PZ_F1137_RUN=snap
f1137-parked-running-mode-tile f1137-parked-running-mode.sh 2 - PZ_F1137_RUN=tile
f1138-float-toggle-screen-hint f1138-float-toggle-screen-hint.sh 2 -
f1139-readopt-keeps-claim f1139-readopt-keeps-claim.sh 2 -
f1140-stale-zone-notice-clear f1140-stale-zone-notice.sh 1 - PZ_F1140_RUN=a
f1140-stale-zone-notice-write f1140-stale-zone-notice.sh 1 - PZ_F1140_RUN=d
f1141-parked-tab-offscreen f1141-parked-tab-offscreen.sh 1 S
f1147-arrival-minimize-float f1147-arrival-minimize-float.sh 2 -
f1149-overlapping-settle-reports f1149-overlapping-settle-reports.sh 4 -
f1151-touch-forgets-claim f1151-touch-forgets-claim.sh 2 -
f1151-scroll-visit-back-to-zone f1151-scroll-visit-back-to-zone.sh 1 -
f1153-cropped-straddler-move f1153-cropped-straddler-move.sh 2 S
f1155-compositor-only-gate f1155-compositor-only-gate.sh 1 -
fdrag-float-keeps-size fdrag-float-keeps-size.sh 1 FS
fsplash-sibling-size-scroll fsplash-sibling-size.sh 1 S PZ_FSPLASH_MODE=scroll
fsplash-sibling-size-snap fsplash-sibling-size.sh 1 S PZ_FSPLASH_MODE=snap
fresize-centred-edge fresize-centred-edge.sh 1 FS
fscale-fixed-device-pixels fscale-fixed-device-pixels.sh 1 S
fu-optout-inview-evacuee fu-optout-inview-evacuee.sh 2 -
fu-optout-hidden-evacuee fu-optout-hidden-evacuee.sh 2 -
fu-optout-cross-screen-move fu-optout-cross-screen-move.sh 2 -
fu-optout-plain-drop fu-optout-plain-drop.sh 2 F
fu-optout-desktop-switched fu-optout-desktop-switched.sh 1 -
fu-optout-tiling-release-bsp fu-optout-tiling-release.sh 1 - PZ_FU_RELEASE_FROM=bsp
fu-optout-tiling-release-scroll fu-optout-tiling-release.sh 1 - PZ_FU_RELEASE_FROM=scroll
fu-optout-float-verbs fu-optout-float-verbs.sh 2 -
fu-optout-minimize fu-optout-minimize.sh 1 -
fu-optout-restart fu-optout-restart.sh 1 -
fu-optout2-drag-overlay fu-optout2-drag-overlay.sh 2 F
fu-optout2-zone-selector fu-optout2-zone-selector.sh 2 F
fu-optout2-snap-release fu-optout2-snap-release.sh 1 -
fu-optout2-strip-release fu-optout2-strip-release.sh 1 -
fu-dbusctx-thumbnail-admitted fu-dbusctx-thumbnail-admitted.sh 1 F kenv=PLASMAZONES_THUMBNAIL_TRACE=1
fu-dbusctx-thumbnail-admitted-pixels fu-dbusctx-thumbnail-admitted.sh 1 F kenv=PLASMAZONES_THUMBNAIL_TRACE=1 kenv=PLASMAZONES_DMABUF_THUMBNAILS=0
fu-dbusctx-spoof-refused fu-dbusctx-spoof-refused.sh 1 -
fu-dbusctx-settings-errors fu-dbusctx-settings-errors.sh 1 -
fu-dbusctx-drop-proxy-owner fu-dbusctx-drop-proxy-owner.sh 1 F
'

usage() { sed -n '7,14p' "$0" | sed 's/^# \{0,1\}//'; }
OUT=""
BUILD="${PZ_NESTED_BUILD:-build}"
LIST=0
REQ=()
while [ $# -gt 0 ]; do
    case "$1" in
    --out)
        [ $# -ge 2 ] || { usage >&2; exit 64; }
        OUT="$2"
        shift 2
        ;;
    --build)
        [ $# -ge 2 ] || { usage >&2; exit 64; }
        BUILD="$2"
        shift 2
        ;;
    --list)
        LIST=1
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    -*)
        echo "unknown option $1" >&2
        usage >&2
        exit 64
        ;;
    *)
        REQ+=("$1")
        shift
        ;;
    esac
done

rows() { printf '%s\n' "$TABLE" | awk 'NF && $1 !~ /^#/'; }
row() { rows | awk -v n="$1" '$1 == n'; }
if [ "$LIST" = 1 ]; then
    rows | awk '{ e = ""; for (i = 5; i <= NF; i++) e = e " " $i; printf "%-44s outputs %s  flags %-4s%s\n", $1, $3, $4, e }'
    exit 0
fi
[ "${#REQ[@]}" -gt 0 ] || { usage >&2; exit 64; }

# The run list: "all" is every row in table order; a probe whose row names after=NAME gets NAME
# run immediately before it.
afterof() { row "$1" | tr ' ' '\n' | sed -n 's/^after=//p'; }
RUN=()
add() {
    local a
    [ -n "$(row "$1")" ] || { echo "unknown probe: $1 (see --list)" >&2; exit 64; }
    a=$(afterof "$1")
    if [ -n "$a" ] && { [ "${#RUN[@]}" = 0 ] || [ "${RUN[-1]}" != "$a" ]; }; then
        add "$a"
    fi
    RUN+=("$1")
}
for r in "${REQ[@]}"; do
    if [ "$r" = all ]; then
        for n in $(rows | awk '{ print $1 }'); do
            add "$n"
        done
    else
        add "$r"
    fi
done

[ -n "${XDG_RUNTIME_DIR:-}" ] || { echo "XDG_RUNTIME_DIR is unset; the nested session needs it" >&2; exit 1; }
[ -d "$REPO/$BUILD/bin" ] || { echo "no build tree at $REPO/$BUILD/bin; build first or pass --build" >&2; exit 1; }
[ -n "$OUT" ] || OUT="$XDG_RUNTIME_DIR/pz-regression/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT" || exit 1
SOCKET="${PZ_NESTED_SOCKET:-pzregress}"
NEST="${PZ_NESTED_DIR:-$XDG_RUNTIME_DIR/pz-regress-nest}"
HOST_DISPLAY="${DISPLAY:-}"

kwin_pid() { pgrep -u "$(id -u)" -f "^kwin_wayland .*--socket $SOCKET( |\$)" | head -1; }
SESSION_PID=""
stop_session() {
    local p
    p=$(kwin_pid)
    [ -n "$p" ] && kill "$p"
    for _ in $(seq 60); do
        if [ -z "$(kwin_pid)" ]; then
            [ -n "$SESSION_PID" ] && wait "$SESSION_PID" 2>/dev/null
            SESSION_PID=""
            return 0
        fi
        sleep 1
    done
    echo "the nested compositor on socket $SOCKET did not exit" >&2
    return 1
}
trap 'stop_session; exit 130' INT TERM

# The fakeinput tool, built once into the build tree when a probe in the run needs it.
FI=""
FI_LOG="$OUT/fakeinput-build.log"
for n in "${RUN[@]}"; do
    case "$(row "$n" | awk '{ print $4 }')" in
    *F*)
        FIB="${PZR_FAKEINPUT_BUILD:-$REPO/$BUILD/regression-fakeinput}"
        echo "building fakeinput into $FIB"
        if { [ -f "$FIB/CMakeCache.txt" ] || cmake -S "$HERE/fakeinput" -B "$FIB" > "$FI_LOG" 2>&1; } \
            && cmake --build "$FIB" >> "$FI_LOG" 2>&1 && [ -x "$FIB/fakeinput" ]; then
            FI="$FIB/fakeinput"
        else
            echo "fakeinput did not build (see $FI_LOG); probes with flag F will not stage" >&2
        fi
        break
        ;;
    esac
done

# launch NAME OUTPUTS FLAGS [KEY=VALUE...]: start the session and its daemon, compositor output
# into NAME.kwin.log; the KEY=VALUE pairs go into the compositor's environment
launch() {
    local name="$1" outputs="$2" flags="$3" up=0 attempt
    shift 3
    local klog="$OUT/$name.kwin.log" slog="$OUT/$name.session.log"
    local envs=(PZ_NESTED_FORCE=1 "PZ_NESTED_BUILD=$BUILD" "PZ_NESTED_DIR=$NEST" "PZ_NESTED_SOCKET=$SOCKET" "$@")
    local size=()
    case "$flags" in *X*) envs+=(PZ_NESTED_XWAYLAND=1) ;; esac
    case "$flags" in *[NF]*) envs+=(KWIN_WAYLAND_NO_PERMISSION_CHECKS=1) ;; esac
    case "$flags" in *K*) envs+=(PZ_NESTED_KEEP_STATE=1) ;; esac
    case "$flags" in *S*) size=(1600 900) ;; esac
    : > "$slog"
    for attempt in $(seq 8); do
        X11_BEFORE=$(ls /tmp/.X11-unix 2>/dev/null | tr '\n' ' ')
        env -u PZ_NESTED_VISIBLE -u PZ_NESTED_XWAYLAND -u PZ_NESTED_KEEP_STATE -u KWIN_WAYLAND_NO_PERMISSION_CHECKS \
            "${envs[@]}" setsid "$REPO/scripts/nested-kwin/run-nested.sh" "$outputs" "${size[@]}" \
            > "$klog" 2>&1 < /dev/null &
        SESSION_PID=$!
        for _ in $(seq 60); do
            if grep -q "need snap restore\|Loaded effect\|plasmazones.effect" "$klog" 2>/dev/null; then
                up=1
                break
            fi
            kill -0 "$SESSION_PID" 2>/dev/null || break
            sleep 1
        done
        [ "$up" = 1 ] && break
        echo "launch attempt $attempt did not come up:" >> "$slog"
        head -5 "$klog" >> "$slog"
        stop_session
        sleep 5
    done
    [ "$up" = 1 ] || return 1
    sleep 3
    # The effect has usually D-Bus-activated a daemon by now, which logs into the compositor's
    # output. daemon.sh evicts it and starts its own, but with activation live the effect can
    # activate another in the gap and take the name first: daemon.sh's daemon then exits ("Already
    # running") and the session runs a daemon whose log is not daemon.log and whose pid is not
    # daemon.pid. So activation stays blocked from here to the session's end (run-nested.sh
    # writes a fresh shim at the next launch), and a daemon that dies mid-probe stays gone, which
    # finish counts as a failure.
    printf '#!/bin/sh\nexit 1\n' > "$NEST/activate-daemon.sh"
    # The block cannot stop an activation already in flight: dbus-daemon may have spawned the shim
    # before it was overwritten, and that daemon can take the name after daemon.sh looked for an
    # owner to evict, so daemon.sh's own daemon exits "Already running". daemon.sh's "daemon up"
    # line names daemon.pid, not the owner, so it does not show this. Hence the ownership check is
    # retried, and when another daemon holds the name daemon.sh runs again: it evicts that owner,
    # and with activation blocked nothing can take its place.
    local try ok=0
    for try in 1 2 3; do
        PZ_NESTED_DIR="$NEST" PZ_NESTED_BUILD="$BUILD" "$REPO/scripts/nested-kwin/daemon.sh" >> "$slog" 2>&1
        for _ in $(seq 10); do
            if daemon_is_ours; then
                ok=1
                break
            fi
            sleep 0.5
        done
        [ "$ok" = 1 ] && break
        echo "daemon.sh run $try: org.plasmazones is owned by pid '$(bus_owner)' (comm '$(cat "/proc/$(bus_owner)/comm" 2>/dev/null)'), daemon.pid is '$(cat "$NEST/daemon.pid" 2>/dev/null)'" >> "$slog"
    done
    [ "$ok" = 1 ]
}
# bus_owner -> the pid owning org.plasmazones on the session's bus, empty when nobody does
bus_owner() {
    (
        . "$NEST/env.sh"
        dbus-send --session --print-reply --dest=org.freedesktop.DBus / \
            org.freedesktop.DBus.GetConnectionUnixProcessID string:org.plasmazones 2>/dev/null | awk '/uint32/ { print $2 }'
    )
}
# daemon_is_ours: the process owning org.plasmazones on the session's bus is daemon.sh's daemon
daemon_is_ours() {
    local owner
    owner=$(bus_owner)
    [ -n "$owner" ] && [ "$owner" = "$(cat "$NEST/daemon.pid" 2>/dev/null)" ] \
        && [ "$(cat "/proc/$owner/comm" 2>/dev/null)" = "plasmazonesd" ]
}

RESULTS=()
FAILED=0
UNSTAGED=0
LAST_NAME=""
LAST_RESULT=""
if [ -n "$(kwin_pid)" ]; then
    echo "a nested session is already running on socket $SOCKET; stopping it"
    stop_session || exit 1
fi
first=1
for name in "${RUN[@]}"; do
    read -r _ script outputs flags extra <<< "$(row "$name")"
    penv=()
    kenv=()
    after=""
    for tok in ${extra:-}; do
        case "$tok" in
        after=*) after="${tok#after=}" ;;
        kenv=*=*) kenv+=("${tok#kenv=}") ;;
        *=*) penv+=("$tok") ;;
        esac
    done
    start=$(date +%s)
    result=""
    reason=""
    nofi=0
    case "$flags" in *F*) [ -n "$FI" ] || nofi=1 ;; esac
    if [ -n "$after" ] && { [ "$LAST_NAME" != "$after" ] || [ "$LAST_RESULT" != PASS ]; }; then
        result=STAGING-FAILED
        reason="needs $after to pass immediately before it"
    elif [ "$nofi" = 1 ]; then
        result=STAGING-FAILED
        reason="fakeinput did not build"
    else
        [ "$first" = 1 ] || sleep "${PZR_SETTLE:-15}"
        first=0
        echo "=== $name: $script, $outputs output(s), flags $flags${penv[*]:+, ${penv[*]}}${kenv[*]:+, compositor ${kenv[*]}}"
        if ! launch "$name" "$outputs" "$flags" "${kenv[@]}"; then
            result=STAGING-FAILED
            reason="the nested session or its daemon did not start (see $name.session.log)"
        else
            env "${penv[@]}" PZR_FAKEINPUT="$FI" PZR_X11_BEFORE="$X11_BEFORE" PZR_HOST_DISPLAY="$HOST_DISPLAY" \
                bash "$HERE/probes/$script" "$NEST" "$OUT/$name.kwin.log" > "$OUT/$name.out" 2>&1 < /dev/null
            rc=$?
            cp "$NEST/daemon.log" "$OUT/$name.daemon.log" 2>/dev/null
            [ -f "$NEST/daemon.log.earlier" ] && cp "$NEST/daemon.log.earlier" "$OUT/$name.daemon-earlier.log"
            reason=$(grep "^RESULT: " "$OUT/$name.out" | tail -1 | sed 's/^RESULT: [A-Z-]* *//')
            case "$rc" in
            0) result=PASS ;;
            1) result=FAIL ;;
            2) result=STAGING-FAILED ;;
            *)
                result=FAIL
                reason="the probe exited with status $rc"
                ;;
            esac
            grep "FAIL: \|^STAGING FAILED: " "$OUT/$name.out" | sed 's/^/  /'
        fi
        stop_session
    fi
    case "$result" in
    FAIL) FAILED=$((FAILED + 1)) ;;
    STAGING-FAILED) UNSTAGED=$((UNSTAGED + 1)) ;;
    esac
    echo "=== $name: $result $reason"
    RESULTS+=("$(printf '%-44s %-15s %4ss  %s' "$name" "$result" "$(($(date +%s) - start))" "$reason")")
    LAST_NAME="$name"
    LAST_RESULT="$result"
done

{
    printf '%-44s %-15s %5s  %s\n' PROBE RESULT TIME DETAIL
    printf '%s\n' "${RESULTS[@]}"
    echo "${#RUN[@]} run: $((${#RUN[@]} - FAILED - UNSTAGED)) passed, $FAILED failed, $UNSTAGED could not stage"
    echo "logs: $OUT"
} | tee "$OUT/summary.txt"
[ "$FAILED" -gt 0 ] && exit 1
[ "$UNSTAGED" -gt 0 ] && exit 2
exit 0
