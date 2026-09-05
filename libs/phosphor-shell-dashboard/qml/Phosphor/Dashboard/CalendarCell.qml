// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Dashboard.CalendarCell, the month grid in the dashboard's
// last row (A3 §7 b).
//
// Same outline and label as a desktop cell. The month's days in 13 px
// tabular figures, seven columns from the locale's first day of the
// week, today in blue (A3 §7 e). `today` is a property so a test can pin
// the month.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property date today: new Date()
    readonly property var locale: Qt.locale()

    readonly property int year: today.getFullYear()
    readonly property int month: today.getMonth()
    readonly property int daysInMonth: new Date(year, month + 1, 0).getDate()
    // Column of the 1st: days from the locale's first weekday.
    // JS counts Sunday as 0; Qt's Locale counts Monday 1 .. Sunday 7.
    readonly property int firstWeekday: new Date(year, month, 1).getDay() === 0 ? 7 : new Date(year, month, 1).getDay()
    readonly property int firstColumn: (firstWeekday - locale.firstDayOfWeek + 7) % 7
    readonly property int rows: Math.ceil((firstColumn + daysInMonth) / 7)

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Calendar, %1").arg(locale.standaloneMonthName(month, Locale.LongFormat) + " " + year)

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_edge
        color: "transparent"
        border.width: 1
        border.color: Spectrum.resting
    }

    TabularText {
        id: label

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Tokens.spacing_s
        text: qsTr("Calendar")
        font.pixelSize: Tokens.font_size_label_m
        color: Theme.on_surface_variant
    }

    TabularText {
        id: monthLabel

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Tokens.spacing_s
        text: root.locale.standaloneMonthName(root.month, Locale.LongFormat) + " " + root.year
        font.pixelSize: Tokens.font_size_label_m
        color: Theme.on_surface
    }

    Grid {
        id: grid

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: label.bottom
        anchors.bottom: parent.bottom
        anchors.margins: Tokens.spacing_s
        columns: 7
        rows: 1 + root.rows

        readonly property real cellW: width / 7
        readonly property real cellH: height / rows

        // Weekday headings from the locale's first day.
        Repeater {
            model: 7
            delegate: Item {
                required property int index
                width: grid.cellW
                height: grid.cellH
                TabularText {
                    anchors.centerIn: parent
                    text: root.locale.dayName(((root.locale.firstDayOfWeek - 1 + parent.index) % 7) + 1, Locale.ShortFormat)
                    font.pixelSize: Tokens.font_size_label_s
                    color: Theme.on_surface_variant
                }
            }
        }

        // Leading blanks, then the days.
        Repeater {
            model: root.firstColumn
            delegate: Item {
                width: grid.cellW
                height: grid.cellH
            }
        }
        Repeater {
            model: root.daysInMonth
            delegate: Item {
                id: day

                required property int index
                readonly property bool isToday: index + 1 === root.today.getDate()

                width: grid.cellW
                height: grid.cellH

                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height) - 2
                    height: width
                    radius: Tokens.radius_mini
                    color: day.isToday ? Qt.rgba(Spectrum.active.r, Spectrum.active.g, Spectrum.active.b, 0.35) : "transparent"
                    border.width: day.isToday ? 1 : 0
                    border.color: Spectrum.active
                }
                TabularText {
                    anchors.centerIn: parent
                    text: day.index + 1
                    font.pixelSize: Tokens.font_size_label_l
                    color: day.isToday ? Theme.on_surface : Theme.on_surface_variant
                }
            }
        }
    }
}
