// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

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
        property int activeWorkspace: 0

        reloadId: "main"
    }

    // Clock formatting via Qt's built-in `Qt.formatDateTime` driven by
    // a 1 s Timer instead of a 1 Hz `date` subprocess. Earlier rev
    // fork/exec'd `date` every second (~86 400 forks/day) for a value
    // QML can compute natively. Sync to the wall-clock minute boundary
    // by leaving the interval at 1 s — the worst-case visual lag from
    // minute roll-over is one second, same as the prior subprocess.
    Item {
        id: clockProc

        property string stdoutText: ""

        function update() {
            stdoutText = Qt.formatDateTime(new Date(), "HH:mm · ddd MMM dd");
        }

        Component.onCompleted: update()

        Timer {
            interval: 1000
            running: true
            repeat: true
            triggeredOnStart: true
            onTriggered: clockProc.update()
        }

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
                return ;

            const fields = line.trim().split(/\s+/).slice(1).map((s) => {
                return parseInt(s, 10);
            });
            // Defensive: a kernel that doesn't expose the expected layout
            // (exotic arch, namespaced /proc) leaves NaN in `fields[3]`,
            // which propagates and parks `prevTotal` at NaN forever.
            if (fields.length < 4 || !Number.isFinite(fields[3]))
                return ;

            // idle = idle + iowait (matches the original awk formula).
            const idle = fields[3] + (fields[4] || 0);
            let total = 0;
            for (const f of fields) {
                if (Number.isFinite(f))
                    total += f;

            }
            if (!Number.isFinite(idle) || !Number.isFinite(total))
                return ;

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

    FileView {
        id: batteryFile

        path: "/sys/class/power_supply/BAT0/capacity"
        interval: 30000
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
        clockText: clockProc.stdoutText.trim()
        cpuPercent: cpuStat.percent
        memPercent: memInfo.percent
        batteryPercent: batteryFile.content.trim()
        batteryVisible: batteryFile.exists
    }

    Taskbar {
    }

    // ─── Popups ──────────────────────────────────────────────────────────
    MenuPopup {
        shellState: shellState
        anchorItem: topPanel.menuAnchor
    }

    CalendarPopup {
        shellState: shellState
        anchorItem: topPanel.calendarAnchor
    }

    // ─── Floating windows ────────────────────────────────────────────────
    SettingsWindow {
        shellState: shellState
        hostname: hostnameFile.content.trim()
    }

}
