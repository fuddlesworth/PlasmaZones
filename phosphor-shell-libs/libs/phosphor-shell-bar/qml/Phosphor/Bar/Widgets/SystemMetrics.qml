// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.SystemMetrics, CPU + memory as bars.
//
// Two 2 × 16 px vertical bars in the rail hue at the chip's x, no labels
// (A2 §5): height is the value, and a bar above 90 % goes white. A 1 px
// peak-hold mark sits at the highest recent reading and releases over
// 4 s. Hover shows the tabular percentages beside the bars.

import QtQuick
import Phosphor.Theme
import Phosphor.Shell
import Phosphor.Widgets

BarWidget {
    id: root

    property real railT: 0.8

    contentWidth: row.implicitWidth
    contentHeight: 16

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("CPU %1 percent, memory %2 percent").arg(usage.cpuPercent).arg(usage.memoryPercent)

    SystemUsage {
        id: usage

        interval: 2000
        enabled: root.visible
    }

    HoverHandler {
        id: hover
    }

    component MetricBar: Item {
        id: bar

        property int percent: 0
        property real peak: 0

        width: 2
        height: 16

        onPercentChanged: {
            if (percent > peak) {
                // A new high jumps instantly; nothing to decay toward yet.
                // Strictly greater, or an idle metric (0 == 0) restarts the
                // 4 s release on every tick and it never actually runs.
                peakRelease.stop();
                peak = percent;
            } else if (peak > percent) {
                // Re-target the decay at the new, lower live value. The `to`
                // binding is read when the animation STARTS, so without a
                // restart here the mark holds at the peak forever: the only
                // previous restart was at the instant peak == percent, where
                // from == to and the run was a no-op.
                peakRelease.restart();
            }
        }

        // Peak releases toward the live value over 4 s.
        NumberAnimation {
            id: peakRelease

            target: bar
            property: "peak"
            to: bar.percent
            duration: 4000
            easing: Motion.release
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: 2
            height: Math.max(1, parent.height * bar.percent / 100)
            color: bar.percent > 90 ? Spectrum.focus : Spectrum.at(root.railT)
            opacity: 0.85

            Behavior on height {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Math.min(parent.height - 1, parent.height * bar.peak / 100)
            width: 2
            height: 1
            color: Spectrum.focus
            opacity: 0.6
        }
    }

    Row {
        id: row

        spacing: 3

        MetricBar {
            percent: usage.cpuPercent
        }
        MetricBar {
            percent: usage.memoryPercent
        }

        TabularText {
            Accessible.ignored: true
            text: usage.cpuPercent + "% " + usage.memoryPercent + "%"
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_label_m
            anchors.verticalCenter: parent.verticalCenter
            visible: opacity > 0
            opacity: hover.hovered ? 1 : 0
            width: hover.hovered ? implicitWidth + Tokens.spacing_xs : 0
            clip: true

            Behavior on opacity {
                NumberAnimation {
                    duration: hover.hovered ? Motion.duration_enter_content : Motion.duration_release
                    easing: hover.hovered ? Motion.reveal : Motion.release
                }
            }
            Behavior on width {
                NumberAnimation {
                    duration: hover.hovered ? Motion.duration_enter_content : Motion.duration_release
                    easing: hover.hovered ? Motion.reveal : Motion.release
                }
            }
        }
    }
}
