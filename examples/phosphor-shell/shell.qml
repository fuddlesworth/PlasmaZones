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

    Process {
        id: cpuProc

        command: ["sh", "-c", "grep 'cpu ' /proc/stat | awk '{usage=($2+$4)*100/($2+$4+$5)} END {printf \"%.0f\", usage}'"]
        running: true
        interval: 2000
    }

    Process {
        id: memProc

        command: ["sh", "-c", "free | awk '/Mem:/ {printf \"%.0f\", $3/$2*100}'"]
        running: true
        interval: 5000
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
        cpuPercent: cpuProc.stdoutText.trim()
        memPercent: memProc.stdoutText.trim()
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
