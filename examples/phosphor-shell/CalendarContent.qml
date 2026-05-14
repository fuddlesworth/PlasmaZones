// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Calendar content — body of the calendar panel popup. Wrapped by
// PanelPopupHost which owns the actual PopupWindow (shared across all
// panel popups so popup-to-popup switching doesn't race xdg_popup
// grab handoff).
Item {
    id: root

    required property var shellState
    // Set true by the host when this is the active content. Drives the
    // scale/opacity Behaviors so the panel still gets the OutBack
    // pop-in transition on each switch.
    property bool active: false

    // Dark-on-gradient palette (matches the top panel text)
    readonly property color colText: "#1e1e2e"
    readonly property color colSubtle: "#45475a"
    readonly property color colMuted: "#585b70"
    readonly property color colAccent: "#7c3aed"
    readonly property color colToday: "#0891b2"
    // Currently displayed month — defaults to today, prev/next nav.
    property date displayDate: new Date()

    scale: active ? 1 : 0.7
    opacity: active ? 1 : 0

    Column {
        anchors.fill: parent
        spacing: 8

        // Header: < Month YYYY >
        Row {
            width: parent.width
            height: 28
            spacing: 8

            Rectangle {
                width: 24
                height: 24
                anchors.verticalCenter: parent.verticalCenter
                radius: 6
                color: prevHover.containsMouse ? "#313244" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "‹"
                    color: root.colText
                    font.pixelSize: 18
                }

                MouseArea {
                    id: prevHover
                    anchors.fill: parent
                    hoverEnabled: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "Previous month"
                    onClicked: {
                        let d = new Date(root.displayDate);
                        d.setMonth(d.getMonth() - 1);
                        root.displayDate = d;
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 64
                horizontalAlignment: Text.AlignHCenter
                text: Qt.formatDate(root.displayDate, "MMMM yyyy")
                color: root.colText
                font.pixelSize: 15
                font.weight: Font.Medium
            }

            Rectangle {
                width: 24
                height: 24
                anchors.verticalCenter: parent.verticalCenter
                radius: 6
                color: nextHover.containsMouse ? "#313244" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "›"
                    color: root.colText
                    font.pixelSize: 18
                }

                MouseArea {
                    id: nextHover
                    anchors.fill: parent
                    hoverEnabled: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "Next month"
                    onClicked: {
                        let d = new Date(root.displayDate);
                        d.setMonth(d.getMonth() + 1);
                        root.displayDate = d;
                    }
                }
            }
        }

        // Day-of-week header row
        Row {
            width: parent.width
            spacing: 0

            Repeater {
                model: 7

                delegate: Text {
                    required property int index

                    width: parent.width / 7
                    horizontalAlignment: Text.AlignHCenter
                    text: Qt.locale().dayName(index, Locale.ShortFormat)
                    color: root.colMuted
                    font.pixelSize: 10
                    font.weight: Font.Medium
                }
            }
        }

        // Month grid — 6 weeks × 7 days = 42 cells.
        Grid {
            width: parent.width
            columns: 7
            rowSpacing: 2
            columnSpacing: 0

            Repeater {
                model: 42

                delegate: Item {
                    required property int index
                    // Anchor every Date to noon to avoid DST off-by-one
                    // boundary flicker.
                    readonly property date cellDate: {
                        let firstOfMonth = new Date(root.displayDate);
                        firstOfMonth.setHours(12, 0, 0, 0);
                        firstOfMonth.setDate(1);
                        let firstDow = firstOfMonth.getDay();
                        let d = new Date(firstOfMonth);
                        d.setDate(1 - firstDow + index);
                        return d;
                    }
                    readonly property bool inCurrentMonth: cellDate.getMonth() === root.displayDate.getMonth()
                    readonly property bool isToday: {
                        let now = new Date();
                        return cellDate.getDate() === now.getDate() && cellDate.getMonth() === now.getMonth() && cellDate.getFullYear() === now.getFullYear();
                    }

                    width: parent.width / 7
                    height: 32

                    Rectangle {
                        anchors.centerIn: parent
                        width: 26
                        height: 26
                        radius: 13
                        color: isToday ? root.colToday : (cellHover.containsMouse ? "#313244" : "transparent")

                        Behavior on color {
                            ColorAnimation {
                                duration: 100
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: cellDate.getDate()
                        color: isToday ? "#cdd6f4" : (inCurrentMonth ? root.colText : root.colMuted)
                        font.pixelSize: 12
                        font.weight: isToday ? Font.Bold : Font.Normal
                    }

                    MouseArea {
                        id: cellHover
                        anchors.fill: parent
                        hoverEnabled: true
                        Accessible.name: cellDate.getDate() + " " + Qt.formatDate(cellDate, "MMMM yyyy")
                    }
                }
            }
        }
    }

    Behavior on scale {
        NumberAnimation {
            duration: 350
            easing.type: Easing.OutBack
            easing.overshoot: 1.4
        }
    }

    Behavior on opacity {
        NumberAnimation {
            duration: 250
        }
    }
}
