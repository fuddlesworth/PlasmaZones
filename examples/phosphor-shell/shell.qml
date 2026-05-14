// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Services 1.0
import Phosphor.Shell 1.0
import QtQuick

// Top-level composer. Owns the shared state and data sources, then
// wires them into the panel/popup/window components living in
// sibling .qml files. Sibling components are auto-discovered by Qt
// from this file's directory.
Item {
    // System data sources — owned at the top level so multiple
    // panels/windows can share a single Process / FileView each.

    // Global UI state. Survives hot-reload via PersistentProperties.
    PersistentProperties {
        id: shellState

        property bool menuOpen: false
        property bool settingsOpen: false
        property bool calendarOpen: false
        property bool mediaOpen: false
        property int activeWorkspace: 0

        reloadId: "main"

        // Popups are mutually exclusive. xdg-popup positioning gets
        // confused when a second popup tries to anchor to the panel
        // while another is still mapped — the new popup ends up
        // chained off the first one's geometry instead of its own
        // anchor item. Close any other open popup before letting a
        // new one map.
        onCalendarOpenChanged: {
            if (calendarOpen) {
                mediaOpen = false;
                menuOpen = false;
            }
        }
        onMediaOpenChanged: {
            if (mediaOpen) {
                calendarOpen = false;
                menuOpen = false;
            }
        }
        onMenuOpenChanged: {
            if (menuOpen) {
                calendarOpen = false;
                mediaOpen = false;
            }
        }
    }

    SystemClock {
        id: clock
        precision: SystemClock.Minutes
    }

    // CPU + memory readouts via /proc — avoids a `sh -c` subprocess
    // every 2-5s (which over a session is hundreds of fork/exec
    // pairs). A FileView re-reads the kernel-exported file in-process
    // at the interval; onContentChanged parses and deltas the values.
    FileView {
        id: cpuStat

        // Cumulative jiffies from the last snapshot (idle + total) for
        // delta computation between intervals. `real` (double) — `int`
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
            for (const line of content.split('\n')) {
                if (line.startsWith('MemTotal:')) {
                    total = parseInt(line.split(/\s+/)[1], 10);
                } else if (line.startsWith('MemAvailable:')) {
                    available = parseInt(line.split(/\s+/)[1], 10);
                    break;
                }
            }
            if (Number.isFinite(total) && Number.isFinite(available) && total > 0)
                percent = Math.round((1 - available / total) * 100).toString();
        }
    }

    // Battery via UPower D-Bus — replaces the raw sysfs FileView.
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
        clockText: {
            const pad = n => n < 10 ? "0" + n : "" + n;
            const days = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
            const months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
            return pad(clock.hours) + ":" + pad(clock.minutes) + " · " + days[clock.date.getDay()] + " " + months[clock.date.getMonth()] + " " + pad(clock.date.getDate());
        }
        cpuPercent: cpuStat.percent
        memPercent: memInfo.percent
        batteryPercent: battery.displayDevice ? Math.round(battery.displayDevice.percentage).toString() : ""
        batteryVisible: battery.displayDevice !== null
    }

    Taskbar {}

    // ─── Popups ──────────────────────────────────────────────────────────
    MenuPopup {
        shellState: shellState
        anchorItem: topPanel.menuAnchor
    }

    CalendarPopup {
        shellState: shellState
        anchorItem: topPanel.calendarAnchor
        panelSurfaceHeight: topPanel.panelSurfaceHeight
    }

    MprisPopup {
        shellState: shellState
        anchorItem: topPanel.mediaAnchor
        currentPlayer: topPanel.mediaPlayer
        panelSurfaceHeight: topPanel.panelSurfaceHeight
    }

    // ─── Floating windows ────────────────────────────────────────────────
    SettingsWindow {
        shellState: shellState
        hostname: hostnameFile.content.trim()
    }
}
