// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.CalendarPanel, the clock chip's transient.
//
// A month grid in tabular figures with today underlined, and nothing
// else. Reading a date is a glance, so this is a transient (A2 §4.7) and
// not a pane: it does not move anyone's windows, and it goes on the next
// click elsewhere.
//
// It is deliberately NOT the dashboard. The dashboard draws a calendar
// cell too (A3 §7), at overview scale, alongside every desktop's
// placement map — that is a different errand, and routing the clock there
// meant a full-screen takeover to answer "what is the date", which is
// what the first implementation shipped.
//
// Today is marked with the 2px spectrum underline rather than a filled
// circle, for the same reason every other value on a Phosphor surface is:
// R1 puts colour in underlines, never in fills.

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Shell

PanelFrame {
    id: root

    title: Qt.formatDate(clock.date, "MMMM yyyy")
    iconName: "view-calendar"
    subtitle: Qt.formatDate(clock.date, Qt.locale().dateFormat(Locale.LongFormat))
    panelWidth: 268

    SystemClock {
        id: clock

        // Minutes, not seconds: nothing here shows a second, and a
        // per-second wakeup for a surface that shows a month would be a
        // timer running for no reason.
        precision: SystemClock.Minutes
    }

    readonly property date _today: clock.date
    readonly property int _year: root._today.getFullYear()
    readonly property int _month: root._today.getMonth()

    // Monday-first offset of the 1st. getDay() is Sunday-based, so the
    // shift maps Sunday's 0 onto 6 and leaves the rest one lower.
    readonly property int _lead: (new Date(root._year, root._month, 1).getDay() + 6) % 7
    readonly property int _days: new Date(root._year, root._month + 1, 0).getDate()

    Item {
        width: parent.width
        implicitHeight: grid.implicitHeight + Tokens.spacing_s

        GridLayout {
            id: grid

            width: parent.width
            columns: 7
            columnSpacing: 0
            rowSpacing: 2

            Repeater {
                model: [qsTr("M"), qsTr("T"), qsTr("W"), qsTr("T"), qsTr("F"), qsTr("S"), qsTr("S")]

                delegate: Text {
                    required property string modelData

                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData
                    color: Theme.on_surface_variant
                    opacity: 0.6
                    font.pixelSize: Tokens.font_size_label_s
                    font.family: Tokens.font_family_ui
                    bottomPadding: Tokens.spacing_xs
                }
            }

            // Blanks before the 1st, so the first row starts on the right
            // weekday. A Repeater over a count of zero mounts nothing,
            // which is the wanted behaviour when the 1st is a Monday.
            Repeater {
                model: root._lead

                delegate: Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                }
            }

            Repeater {
                model: root._days

                delegate: Item {
                    id: day

                    required property int index

                    readonly property int _date: day.index + 1
                    readonly property bool _isToday: day._date === root._today.getDate()

                    Layout.fillWidth: true
                    Layout.preferredHeight: 26

                    TabularText {
                        anchors.centerIn: parent
                        text: day._date
                        // Today is white, because focus and urgency are
                        // white and never a hue (R9). Everything else is
                        // the ordinary body colour.
                        color: day._isToday ? Theme.on_surface : Theme.on_surface_variant
                        font.pixelSize: Tokens.font_size_body_s
                        font.weight: day._isToday ? Tokens.font_weight_medium : Tokens.font_weight_regular
                    }

                    Rectangle {
                        visible: day._isToday
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                        width: 14
                        height: 2
                        color: Spectrum.at(root.railT)
                    }
                }
            }
        }
    }
}
