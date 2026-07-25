// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.Clock, the time + date bar widget.
//
// Self-contained: owns a SystemClock (minute precision) and renders the
// time and locale-formatted date. The centre widget in the default bar
// layout. Time is locale-neutral HH:mm; the date follows the locale's
// short format so day/month ordering matches the user's locale.

import QtQuick
import Phosphor.Theme
import Phosphor.Shell

Item {
    id: root

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    SystemClock {
        id: clock

        precision: SystemClock.Minutes
    }

    // SystemClock exposes only a QDate (clock.date), so the time is
    // assembled from hours/minutes. The `< 0` branch guards the pre-tick
    // sentinel that SystemClock's synchronous first update makes
    // unobservable in practice; kept against future refactors.
    readonly property string _time: clock.hours < 0 ? "" : String(clock.hours).padStart(2, "0") + ":" + String(clock.minutes).padStart(2, "0")
    readonly property string _date: Qt.formatDate(clock.date, Qt.locale().dateFormat(Locale.ShortFormat))

    Accessible.role: Accessible.StaticText
    Accessible.name: root._time + " " + root._date

    Row {
        id: row

        spacing: Tokens.spacing_s

        Text {
            id: timeLabel

            // Folded into the root's Accessible.name already; QQuickText
            // exposes itself as its own StaticText node, so without this
            // assistive tech reads the composed name and then re-reads
            // this fragment.
            Accessible.ignored: true
            text: root._time
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_title_s
            font.weight: Tokens.font_weight_medium
            font.family: Tokens.font_family
        }

        Text {
            Accessible.ignored: true
            text: "·"
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_title_s
            font.family: Tokens.font_family
            anchors.verticalCenter: timeLabel.verticalCenter
        }

        Text {
            Accessible.ignored: true
            text: root._date
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_s
            font.family: Tokens.font_family
            anchors.verticalCenter: timeLabel.verticalCenter
        }
    }
}
