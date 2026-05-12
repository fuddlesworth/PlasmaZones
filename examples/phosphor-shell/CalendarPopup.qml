// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

// Calendar popup — click the clock → MonthGrid scales/fades in via QML
// scale + opacity Behaviors with an OutBack-overshoot easing. Demonstrates
// PopupWindow anchored to a panel item via Wayland xdg-popup with the
// frosted-glass shader for the backdrop.
PopupWindow {
    id: root

    required property var shellState
    required property var anchorItem

    anchor: anchorItem
    popupEdge: PopupWindow.Below
    popupWidth: 280
    popupHeight: 320
    gap: 8
    // popupVisible is imperative-driven from shellState.calendarOpen
    // rather than a direct binding: when the Wayland compositor
    // dismisses the popup on outside-click, it writes popupVisible
    // to false from C++; a direct binding `popupVisible: shellState.<bool>`
    // immediately re-evaluates to true and the popup re-maps, looping
    // every outside click. Connections handler keeps state in sync
    // both directions without the loop.
    popupVisible: false
    onPopupVisibleChanged: {
        // Compositor-side dismissal — propagate back so the QML
        // controlling state matches the actual surface state.
        if (!popupVisible && shellState.calendarOpen)
            shellState.calendarOpen = false;

    }

    Connections {
        function onCalendarOpenChanged() {
            root.popupVisible = shellState.calendarOpen;
        }

        target: shellState
    }

    // Backdrop — frosted-glass shader only. An opaque base Rectangle
    // used to live here as a "shader still warming" fallback, but it
    // composited UNDER the 85%-opaque shader and the popup ended up
    // looking like a solid dark-blue block with the wallpaper
    // completely masked. The shader compiles synchronously by the
    // time the popup maps, so the fallback wasn't paying for itself.
    ShaderBackground {
        // tintOpacity
        // noiseAmount
        // noiseScale
        // animSpeed
        // cornerRadius (px)

        anchors.fill: parent
        playing: root.popupVisible
        shaderSource: Qt.resolvedUrl("shaders/frosted_glass.frag")
        // ShaderEffect::setShaderParams only honours canonical
        // `customParams<N>_<x|y|z|w>` and `customColor<N>` keys —
        // friendly names like "tintOpacity" / "cornerRadius" are
        // silently dropped. Map to the slot layout documented in
        // frosted_glass.frag (customParams[0] = tint/noise/anim,
        // customParams[1].x = cornerRadius).
        shaderParams: {
            "customParams1_x": 0.55,
            "customParams1_y": 0.14,
            "customParams1_z": 22,
            "customParams1_w": 0.4,
            "customParams2_x": 14
        }
        customColor1: "#1e1e2e"
    }

    // Hairline border — kept as a Qt Quick Rectangle (not part of the
    // shader) because the shader's SDF mask AA already handles the
    // corner rounding. The translucent border just defines the popup
    // edge against busy wallpapers.
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: 14
        border.color: "#80a6adc8"
        border.width: 1
    }

    // Calendar content — scale + opacity Behaviors give the visible
    // "pop in" transition (OutBack easing for the bounce).
    Item {
        id: calendarSource

        // Catppuccin palette
        readonly property color colText: "#cdd6f4"
        readonly property color colSubtle: "#a6adc8"
        readonly property color colMuted: "#6c7086"
        readonly property color colAccent: "#cba6f7"
        readonly property color colToday: "#89dceb"
        // Currently displayed month — defaults to today, prev/next nav.
        property date displayDate: new Date()

        anchors.fill: parent
        anchors.margins: 14
        scale: root.shellState.calendarOpen ? 1 : 0.7
        opacity: root.shellState.calendarOpen ? 1 : 0

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
                        color: calendarSource.colText
                        font.pixelSize: 18
                    }

                    MouseArea {
                        id: prevHover

                        anchors.fill: parent
                        hoverEnabled: true
                        Accessible.role: Accessible.Button
                        Accessible.name: "Previous month"
                        onClicked: {
                            let d = new Date(calendarSource.displayDate);
                            d.setMonth(d.getMonth() - 1);
                            calendarSource.displayDate = d;
                        }
                    }

                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 64
                    horizontalAlignment: Text.AlignHCenter
                    text: Qt.formatDate(calendarSource.displayDate, "MMMM yyyy")
                    color: calendarSource.colText
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
                        color: calendarSource.colText
                        font.pixelSize: 18
                    }

                    MouseArea {
                        id: nextHover

                        anchors.fill: parent
                        hoverEnabled: true
                        Accessible.role: Accessible.Button
                        Accessible.name: "Next month"
                        onClicked: {
                            let d = new Date(calendarSource.displayDate);
                            d.setMonth(d.getMonth() + 1);
                            calendarSource.displayDate = d;
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
                        color: calendarSource.colMuted
                        font.pixelSize: 10
                        font.weight: Font.Medium
                    }

                }

            }

            // Month grid — 6 weeks × 7 days = 42 cells. Each cell shows
            // day-of-month for displayDate's month, with leading/trailing
            // days from neighbour months greyed out.
            Grid {
                width: parent.width
                columns: 7
                rowSpacing: 2
                columnSpacing: 0

                Repeater {
                    model: 42

                    delegate: Item {
                        required property int index
                        // Day 0 = first cell = first day of week containing the 1st.
                        // Anchor every Date to noon to avoid DST off-by-one
                        // boundary flicker: at midnight on a spring-forward
                        // day, `setDate` may shift across the DST boundary
                        // and `getDate()` then reflects the wrong month for
                        // the leading cell.
                        readonly property date cellDate: {
                            let firstOfMonth = new Date(calendarSource.displayDate);
                            firstOfMonth.setHours(12, 0, 0, 0);
                            firstOfMonth.setDate(1);
                            let firstDow = firstOfMonth.getDay();
                            let d = new Date(firstOfMonth);
                            d.setDate(1 - firstDow + index);
                            return d;
                        }
                        readonly property bool inCurrentMonth: cellDate.getMonth() === calendarSource.displayDate.getMonth()
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
                            color: isToday ? calendarSource.colToday : (cellHover.containsMouse ? "#313244" : "transparent")

                            Behavior on color {
                                ColorAnimation {
                                    duration: 100
                                }

                            }

                        }

                        Text {
                            anchors.centerIn: parent
                            text: cellDate.getDate()
                            color: isToday ? "#1e1e2e" : (inCurrentMonth ? calendarSource.colText : calendarSource.colMuted)
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

}
