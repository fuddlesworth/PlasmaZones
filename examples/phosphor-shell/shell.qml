// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

// Top-level composer. Owns the shared state and data sources, then
// wires them into the panel/popup/window components living in
// sibling .qml files. Sibling components are auto-discovered by Qt
// from this file's directory.
Item {
    // Global UI state. Survives hot-reload via PersistentProperties.
    PersistentProperties {
        id: shellState

        property bool menuOpen: false
        property bool settingsOpen: false
        property bool calendarOpen: false
        property int activeWorkspace: 0

        reloadId: "main"
    }

    // System data sources — owned at the top level so multiple
    // panels/windows can share a single Process / FileView each.
    Process {
        id: clockProc

        command: ["date", "+%H:%M · %a %b %d"]
        running: true
        interval: 10000
    }

    // CPU + memory readouts via /proc — avoids a `sh -c` subprocess
    // every 2-5s (which over a session is hundreds of fork/exec
    // pairs). A FileView re-reads the kernel-exported file in-process
    // at the interval; onContentChanged parses and deltas the values.
    FileView {
        id: cpuStat

        // Cumulative jiffies from the last snapshot (idle + total) for
        // delta computation between intervals.
        property int prevIdle: 0
        property int prevTotal: 0
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
            // idle = idle + iowait (matches the original awk formula).
            const idle = fields[3] + (fields[4] || 0);
            let total = 0;
            for (const f of fields) {
                total += f;
            }
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
            if (total > 0)
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
    LeftPanel {
        id: leftPanel

        shellState: shellState
    }

    CenterPanel {
        id: centerPanel

        shellState: shellState
        clockText: clockProc.stdoutText.trim()
    }

    RightPanel {
        shellState: shellState
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
        anchorItem: leftPanel.menuAnchor
    }

    CalendarPopup {
        shellState: shellState
        anchorItem: centerPanel.calendarAnchor
    }

    // ─── Floating windows ────────────────────────────────────────────────
    SettingsWindow {
        shellState: shellState
        hostname: hostnameFile.content.trim()
    }

}
