// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Clock, the time bar widget.
//
// Self-contained: owns a SystemClock (minute precision). HH:MM in the
// mono face with tabular figures, so the chip never reflows; the minute
// digit's change is marked by the 2 px spectrum underline tick (05 R7).
// Hover reveals the locale-formatted date to the right.

import QtQuick
import Phosphor.Theme
import Phosphor.Shell
import Phosphor.Widgets

BarWidget {
    id: root

    // Rail-axis hue of this chip, bound by the slot that mounts it.
    property real railT: 0.5

    SystemClock {
        id: clock

        precision: SystemClock.Minutes
    }

    readonly property string _time: clock.hours < 0 ? "" : String(clock.hours).padStart(2, "0") + ":" + String(clock.minutes).padStart(2, "0")
    readonly property string _date: Qt.formatDate(clock.date, Qt.locale().dateFormat(Locale.ShortFormat))

    contentWidth: row.implicitWidth
    contentHeight: row.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root._time + " " + root._date

    HoverHandler {
        id: hover
    }

    Row {
        id: row

        spacing: Tokens.spacing_s

        TabularText {
            id: timeLabel

            Accessible.ignored: true
            text: root._time
            font.pixelSize: Tokens.font_size_title_s
            font.weight: Tokens.font_weight_medium
            tickOnChange: true
            t: root.railT
        }

        Text {
            Accessible.ignored: true
            text: root._date
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_s
            font.family: Tokens.font_family_ui
            anchors.verticalCenter: timeLabel.verticalCenter
            visible: opacity > 0
            opacity: hover.hovered ? 1 : 0
            width: hover.hovered ? implicitWidth : 0
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
