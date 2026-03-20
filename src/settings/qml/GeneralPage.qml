// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    contentHeight: content.implicitHeight

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Animations")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                CheckBox {
                    text: i18n("Enable animations")
                    checked: kcm.animationsEnabled
                    onToggled: kcm.animationsEnabled = checked
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Duration (ms):")
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 50
                        to: 500
                        stepSize: 10
                        value: kcm.animationDuration
                        onMoved: kcm.animationDuration = value
                    }

                    Label {
                        text: Math.round(kcm.animationDuration)
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Sequence mode:")
                    }

                    ComboBox {
                        Layout.fillWidth: true
                        model: [i18n("Simultaneous"), i18n("Staggered")]
                        currentIndex: kcm.animationSequenceMode
                        onActivated: kcm.animationSequenceMode = currentIndex
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Min distance (px):")
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 200
                        stepSize: 1
                        value: kcm.animationMinDistance
                        onMoved: kcm.animationMinDistance = value
                    }

                    Label {
                        text: kcm.animationMinDistance
                    }

                }

            }

        }

        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("OSD")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                CheckBox {
                    text: i18n("Show OSD on layout switch")
                    checked: kcm.showOsdOnLayoutSwitch
                    onToggled: kcm.showOsdOnLayoutSwitch = checked
                }

                CheckBox {
                    text: i18n("Show navigation OSD")
                    checked: kcm.showNavigationOsd
                    onToggled: kcm.showNavigationOsd = checked
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("OSD style:")
                    }

                    ComboBox {
                        Layout.fillWidth: true
                        model: [i18n("None"), i18n("Minimal"), i18n("Full")]
                        currentIndex: kcm.osdStyle
                        onActivated: kcm.osdStyle = currentIndex
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Overlay display mode:")
                    }

                    ComboBox {
                        Layout.fillWidth: true
                        model: [i18n("Overlay"), i18n("Full")]
                        currentIndex: kcm.overlayDisplayMode
                        onActivated: kcm.overlayDisplayMode = currentIndex
                    }

                }

            }

        }

    }

}
