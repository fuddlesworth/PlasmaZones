// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.SystemMetrics, CPU + memory usage readouts.
//
// Self-contained: two in-process FileView reads of /proc avoid a `sh -c`
// subprocess every interval (hundreds of fork/exec pairs over a session).
// CPU% is a jiffy delta between snapshots; memory% is 1 - MemAvailable /
// MemTotal. Both parse defensively and skip an update on an unexpected
// /proc layout rather than freezing on a bogus value.

import QtQuick
import Phosphor.Theme
import Phosphor.Shell

Item {
    id: root

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: "CPU " + cpuStat.percent + " percent, memory " + memInfo.percent + " percent"

    FileView {
        id: cpuStat

        // `real` (double): a 32-bit QML int overflows the cumulative jiffy
        // total on a multi-core box at 100 Hz within days, after which the
        // delta goes bogus.
        property real prevIdle: 0
        property real prevTotal: 0
        property string percent: "0"

        path: "/proc/stat"
        interval: 2000
        onContentChanged: {
            const line = content.split('\n')[0];
            if (!line.startsWith('cpu '))
                return;
            const fields = line.trim().split(/\s+/).slice(1).map(s => parseInt(s, 10));
            if (fields.length < 4 || !Number.isFinite(fields[3])) {
                console.warn("SystemMetrics: unexpected /proc/stat layout, skipping update");
                return;
            }
            const idle = fields[3] + (fields[4] || 0);
            let total = 0;
            for (const f of fields) {
                if (!Number.isFinite(f)) {
                    console.warn("SystemMetrics: non-finite jiffy field, skipping update");
                    return;
                }
                total += f;
            }
            if (cpuStat.prevTotal > 0) {
                const dTotal = total - cpuStat.prevTotal;
                const dIdle = idle - cpuStat.prevIdle;
                // dTotal <= 0 re-baselines silently on a counter reset /
                // wrap (suspend-resume snapshot going backwards).
                if (dTotal > 0)
                    cpuStat.percent = Math.max(0, Math.min(100, Math.round((1 - dIdle / dTotal) * 100))).toString();
            }
            cpuStat.prevIdle = idle;
            cpuStat.prevTotal = total;
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
                const trimmed = line.trim();
                if (trimmed.startsWith('MemTotal:')) {
                    total = parseInt(trimmed.replace(/^\w+:\s*/, '').split(/\s+/)[0], 10);
                } else if (trimmed.startsWith('MemAvailable:')) {
                    available = parseInt(trimmed.replace(/^\w+:\s*/, '').split(/\s+/)[0], 10);
                    foundAvailable = true;
                    break;
                }
            }
            // Require foundAvailable: a kernel without MemAvailable would
            // otherwise leave available at 0 and report a bogus 100%.
            if (foundAvailable && Number.isFinite(total) && Number.isFinite(available) && total > 0)
                memInfo.percent = Math.max(0, Math.min(100, Math.round((1 - available / total) * 100))).toString();
        }
    }

    Row {
        id: row

        spacing: Tokens.spacing_m

        Text {
            text: "CPU " + cpuStat.percent + "%"
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_label_m
            font.family: Tokens.font_family
        }

        Text {
            text: "MEM " + memInfo.percent + "%"
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_label_m
            font.family: Tokens.font_family
        }
    }
}
