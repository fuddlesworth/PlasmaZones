// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.SystemMetrics, CPU + memory usage readouts.
//
// Self-contained: owns a SystemUsage, which samples /proc in-process (no
// subprocess per tick) and publishes whole percents. The jiffy-delta
// arithmetic and the malformed-layout handling live in C++, so this file
// is presentation only.
//
// Both labels reserve the width of their widest reading ("100%"), so a
// value crossing a digit boundary does not relayout the chip and, through
// it, the whole bar row every couple of seconds.

import QtQuick
import Phosphor.Theme
import Phosphor.Shell

Item {
    id: root

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("CPU %1 percent, memory %2 percent").arg(usage.cpuPercent).arg(usage.memoryPercent)

    SystemUsage {
        id: usage

        interval: 2000
        // Stop sampling while the widget is not mounted in a visible chip.
        // This tracks the QML item tree only, not output power or occlusion,
        // so a visible-but-covered bar keeps sampling.
        enabled: root.visible
    }

    // Each label pins to the width of its OWN widest reading. One shared
    // measurement would mis-size the other label in a proportional font
    // (the system font is), and could clip it since neither elides.
    TextMetrics {
        id: widestCpu

        font.pixelSize: Tokens.font_size_label_m
        font.family: Tokens.font_family
        text: "CPU 100%"
    }

    TextMetrics {
        id: widestMem

        font.pixelSize: Tokens.font_size_label_m
        font.family: Tokens.font_family
        text: "MEM 100%"
    }

    Row {
        id: row

        spacing: Tokens.spacing_m

        Text {
            // Folded into the root's Accessible.name already; QQuickText
            // exposes itself as its own StaticText node, so without this
            // assistive tech reads the composed name and then re-reads
            // this fragment.
            Accessible.ignored: true
            text: "CPU " + usage.cpuPercent + "%"
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_label_m
            font.family: Tokens.font_family
            width: Math.ceil(widestCpu.advanceWidth)
            horizontalAlignment: Text.AlignLeft
        }

        Text {
            Accessible.ignored: true
            text: "MEM " + usage.memoryPercent + "%"
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_label_m
            font.family: Tokens.font_family
            width: Math.ceil(widestMem.advanceWidth)
            horizontalAlignment: Text.AlignLeft
        }
    }
}
