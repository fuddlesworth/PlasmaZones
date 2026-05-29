// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Service.UPower 1.0
import Phosphor.Shell 1.0
import QtQuick

// Top-level composer. Owns the shared state and data sources, then
// wires them into the panel/popup/window components living in
// sibling .qml files. Sibling components are auto-discovered by Qt
// from this file's directory.
Item {
    // System data sources: owned at the top level so multiple
    // panels/windows can share a single Process / FileView each.

    Component.onCompleted: shellRouter.togglePopup = panelPopupHost.toggle

    // Persistent UI state that survives hot-reload. The persistence
    // path serialises typed POD properties only (PersistentProperties
    // silently drops anything that isn't bool/int/string/list/map/date,
    // see PersistentProperties.cpp), so a `property var togglePopup`
    // alongside the bools would be a foot-gun: it would look like part
    // of the persistent set but never get saved across hot-reloads.
    // The router below holds non-persistent fields (the togglePopup
    // function ref) and proxies the rest through to `shellState`. From
    // a consumer's perspective both shell-wide state and the router
    // surface the same name (`shellState`), via the alias declared
    // below.
    PersistentProperties {
        id: shellStatePersistent

        property bool menuOpen: false
        property bool settingsOpen: false
        property bool calendarOpen: false
        property bool mediaOpen: false
        property int activeWorkspace: 0

        reloadId: "main"
    }

    QtObject {
        id: shellRouter

        // Property aliases pass reads AND writes straight through to
        // the underlying PersistentProperties storage. No binding-
        // break-on-write hazard (which a `property bool x: persistent.x`
        // initial binding would carry).
        property alias menuOpen: shellStatePersistent.menuOpen
        property alias settingsOpen: shellStatePersistent.settingsOpen
        property alias calendarOpen: shellStatePersistent.calendarOpen
        property alias mediaOpen: shellStatePersistent.mediaOpen
        property alias activeWorkspace: shellStatePersistent.activeWorkspace
        // Function reference assigned in Component.onCompleted. TopPanel
        // and widget MouseAreas call this to toggle panel popups; the
        // host swaps the active popup in-place inside a single shared
        // xdg_popup so popup-to-popup transitions can't race the Wayland
        // grab handoff. Function refs are NOT persisted (Persistent-
        // Properties drops non-POD types on save), which is why
        // togglePopup lives on the router rather than alongside the
        // bool/int state.
        property var togglePopup
    }

    property alias shellState: shellRouter

    SystemClock {
        id: clock

        precision: SystemClock.Minutes
    }

    // CPU + memory readouts via /proc: avoids a `sh -c` subprocess
    // every 2-5s (which over a session is hundreds of fork/exec
    // pairs). A FileView re-reads the kernel-exported file in-process
    // at the interval; onContentChanged parses and deltas the values.
    FileView {
        id: cpuStat

        // Cumulative jiffies from the last snapshot (idle + total) for
        // delta computation between intervals. `real` (double): `int`
        // is 32-bit signed in QML, and on a multi-core box at 100Hz the
        // total jiffies cross INT32_MAX in days, after which assignment
        // truncates and the next delta is bogus.
        property real prevIdle: 0
        property real prevTotal: 0
        // Computed % usage, exposed as a string for the panel binding.
        property string percent: "0"

        path: "/proc/stat"
        interval: 2000
        onContentChanged: {
            // First line: "cpu  user nice system idle iowait irq softirq steal guest guest_nice"
            const line = content.split('\n')[0];
            if (!line.startsWith('cpu '))
                return;

            const fields = line.trim().split(/\s+/).slice(1).map(s => {
                return parseInt(s, 10);
            });
            // Defensive: a kernel that doesn't expose the expected layout
            // (exotic arch, namespaced /proc) leaves NaN in `fields[3]`,
            // which propagates and parks `prevTotal` at NaN forever.
            if (fields.length < 4 || !Number.isFinite(fields[3]))
                return;

            // idle = idle + iowait (matches the original awk formula).
            const idle = fields[3] + (fields[4] || 0);
            let total = 0;
            for (const f of fields) {
                if (Number.isFinite(f))
                    total += f;
            }
            if (!Number.isFinite(idle) || !Number.isFinite(total))
                return;

            if (prevTotal > 0) {
                const dTotal = total - prevTotal;
                const dIdle = idle - prevIdle;
                if (dTotal > 0)
                    percent = Math.round((1 - dIdle / dTotal) * 100).toString();
            }
            prevIdle = idle;
            prevTotal = total;
        }
    }

    FileView {
        id: memInfo

        property string percent: "0"

        path: "/proc/meminfo"
        interval: 5000
        onContentChanged: {
            let total = 0;
            let available = 0;
            let foundAvailable = false;
            for (const line of content.split('\n')) {
                // Trim leading whitespace first: a containerised or
                // future-kernel /proc/meminfo line with a leading space
                // would make split(/\s+/)[0] empty and [1] the label
                // ("MemTotal:"), which parseInt would silently turn
                // into NaN and the Number.isFinite guard below would
                // skip the update with no visible cause.
                const trimmed = line.trim();
                if (trimmed.startsWith('MemTotal:')) {
                    total = parseInt(trimmed.split(/\s+/)[1], 10);
                } else if (trimmed.startsWith('MemAvailable:')) {
                    available = parseInt(trimmed.split(/\s+/)[1], 10);
                    foundAvailable = true;
                    break;
                }
            }
            // Require `foundAvailable`: on a kernel without MemAvailable
            // (kernel < 3.14, exotic embedded build) or a malformed
            // /proc/meminfo, available would stay at 0 and the guard
            // below would yield a bogus 100% reading.
            if (foundAvailable && Number.isFinite(total) && Number.isFinite(available) && total > 0)
                percent = Math.round((1 - available / total) * 100).toString();
        }
    }

    // Battery via UPower D-Bus: replaces the raw sysfs FileView.
    // UPowerHost connects to org.freedesktop.UPower on the system bus;
    // displayDevice is the aggregate battery (percentage, state, icon).
    UPowerHost {
        id: battery
    }

    FileView {
        id: hostnameFile

        path: "/etc/hostname"
    }

    // ─── Panels ──────────────────────────────────────────────────────────
    // Single continuous top panel with left/center/right anchored zones.
    // One layer-shell surface, one exclusive zone, one shader.
    TopPanel {
        id: topPanel

        shellState: shellState
        // Time as locale-neutral HH:mm; the date portion via Qt.formatDate
        // so day/month names follow the user's locale (the hand-rolled
        // English arrays this replaced were an i18n regression).
        clockText: {
            const pad = n => {
                return n < 10 ? "0" + n : "" + n;
            };
            return pad(clock.hours) + ":" + pad(clock.minutes) + " · " + Qt.formatDate(clock.date, "ddd MMM dd");
        }
        cpuPercent: cpuStat.percent
        memPercent: memInfo.percent
        batteryPercent: battery.displayDevice ? Math.round(battery.displayDevice.percentage).toString() : ""
        batteryVisible: battery.displayDevice !== null
    }

    Taskbar {}

    // ─── Popups ──────────────────────────────────────────────────────────
    // Single shared xdg_popup that hosts the calendar / media / menu
    // contents. See PanelPopupHost.qml.
    PanelPopupHost {
        id: panelPopupHost

        shellState: shellState
        topPanel: topPanel
    }

    // ─── Floating windows ────────────────────────────────────────────────
    SettingsWindow {
        shellState: shellState
        hostname: hostnameFile.content.trim()
    }
}
