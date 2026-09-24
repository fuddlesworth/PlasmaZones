// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

GridLayout {
    id: root
    required property var controller
    columns: width < 640 ? 1 : 2
    columnSpacing: 18
    rowSpacing: 18
    component Heading: ColumnLayout {
        property string title
        property string subtitle: ""
        spacing: 6
        LookText {
            text: parent.title
            size: 13
            font.weight: Font.Medium
        }
        LookText {
            text: parent.subtitle
            muted: true
            size: 9
            visible: text.length > 0
        }
    }
    Rectangle {
        Layout.columnSpan: root.columns
        Layout.fillWidth: true
        implicitHeight: 200
        radius: Math.max(8, Appearance.radius * .7)
        border.color: Appearance.outline
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: Appearance.recess
            }
            GradientStop {
                position: 1
                color: Qt.tint(Appearance.surface, Qt.alpha(Appearance.stops[2], .19))
            }
        }
        Column {
            x: 26
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16
            LookText {
                text: qsTr("BUILT AROUND YOUR WINDOWS")
                kicker: true
            }
            LookText {
                text: qsTr("Find your balance.")
                size: 23
            }
            LookText {
                text: qsTr("Color, texture and a little breathing room.")
                muted: true
                size: 10
            }
        }
        Rectangle {
            visible: root.width >= 640
            x: parent.width - width - 36
            y: 27
            width: Math.min(335, parent.width * .43)
            height: 140
            radius: Appearance.radius
            rotation: -2
            color: Appearance.surface
            border.color: Qt.alpha(Appearance.stops[0], .65)
            Column {
                x: 16
                y: 15
                spacing: 14
                width: parent.width - 32
                LookText {
                    text: "⌘  Shell.qml"
                    size: 8
                    muted: true
                }
                LookText {
                    text: qsTr("Light defines the edges.")
                    size: 14
                }
                LookText {
                    text: qsTr("Everything else gets room to breathe.")
                    muted: true
                    size: 9
                }
                Row {
                    spacing: 7
                    ShellButton {
                        text: qsTr("Comfortable")
                        labelSize: 8
                        implicitHeight: 23
                    }
                    ShellButton {
                        text: qsTr("Focused")
                        highlighted: true
                        labelSize: 8
                        implicitHeight: 23
                    }
                }
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            width: 176
            height: 36
            radius: 13
            color: Appearance.card
            border.color: Appearance.outline
            ShellIcon {
                x: 12
                y: 12
                width: 12
                height: 12
                source: "audio-volume-high"
                color: Appearance.text
            }
            Rectangle {
                x: 36
                y: 16
                width: 100
                height: 4
                radius: 2
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop {
                        position: 0
                        color: Appearance.stops[0]
                    }
                    GradientStop {
                        position: 1
                        color: Appearance.stops[3]
                    }
                }
            }
            LookText {
                x: 146
                y: 11
                text: "64"
                size: 8
            }
        }
    }
    LookCard {
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.fillHeight: true
        Layout.minimumHeight: 251
        Heading {
            title: qsTr("Color field")
            subtitle: qsTr("Four related colors carry through the shell.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [
                    {
                        id: "spectrum",
                        name: qsTr("Phosphor"),
                        description: qsTr("The original spectrum"),
                        colors: ["#41d4e8", "#6e9cfd", "#b68aee", "#f390b3"]
                    },
                    {
                        id: "wallpaper",
                        name: qsTr("Wallpaper"),
                        description: qsTr("Drawn from your background"),
                        colors: Appearance.settings.wallpaperColors
                    },
                    {
                        id: "ember",
                        name: qsTr("Warm"),
                        description: qsTr("Honey, clay and rose"),
                        colors: ["#e8c988", "#d4b08a", "#d3906c", "#d27b83"]
                    }
                ]
                Basic.AbstractButton {
                    id: source
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 80
                    Accessible.name: modelData.name
                    onClicked: {
                        AppearanceStore.setValue("palette", modelData.id);
                        const walls = Appearance.settings.wallpapers;
                        const wall = walls[root.controller.selectedScreen] || walls[""];
                        if (modelData.id === "wallpaper" && wall)
                            AppearanceLibrary.chooseWallpaper(wall.path, root.controller.selectedScreen, wall.fit);
                    }
                    background: Rectangle {
                        radius: 7
                        color: Appearance.settings.palette === source.modelData.id ? Qt.alpha(Appearance.accent, .11) : "transparent"
                        border.color: source.visualFocus ? Appearance.text : Appearance.settings.palette === source.modelData.id ? Appearance.accent : Appearance.outline
                    }
                    contentItem: Item {
                        Row {
                            x: 8
                            y: 8
                            width: parent.width - 16
                            height: 20
                            Repeater {
                                model: source.modelData.colors
                                Rectangle {
                                    required property var modelData
                                    width: parent.width / 4
                                    height: 20
                                    color: modelData
                                }
                            }
                        }
                        LookText {
                            x: 8
                            y: 39
                            text: source.modelData.name
                            size: 10
                        }
                        LookText {
                            x: 8
                            y: 56
                            width: parent.width - 16
                            text: source.modelData.description
                            size: 7
                            muted: true
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 7
            LookText {
                text: qsTr("Focus accent")
                size: 9
                muted: true
                Layout.fillWidth: true
            }
            Repeater {
                model: 4
                Basic.AbstractButton {
                    id: accent
                    required property int index
                    implicitWidth: 24
                    implicitHeight: 24
                    Accessible.name: qsTr("Use color %1 for focus").arg(index + 1)
                    onClicked: AppearanceStore.setValue("accentIndex", index)
                    background: Rectangle {
                        radius: 12
                        color: Appearance.stops[accent.index]
                        border.width: accent.visualFocus || Appearance.settings.accentIndex === accent.index ? 2 : 0
                        border.color: Appearance.text
                    }
                    contentItem: Text {
                        text: Appearance.settings.accentIndex === accent.index ? "✓" : ""
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: Appearance.recess
                    }
                }
            }
        }
    }
    LookCard {
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.fillHeight: true
        Layout.minimumHeight: 251
        Heading {
            title: qsTr("Material")
            subtitle: qsTr("Give each surface its own weight.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [
                    {
                        id: "glass",
                        name: qsTr("Glass"),
                        description: qsTr("Tinted and translucent")
                    },
                    {
                        id: "solid",
                        name: qsTr("Solid"),
                        description: qsTr("Quiet and opaque")
                    },
                    {
                        id: "light",
                        name: qsTr("Paper"),
                        description: qsTr("Light and softly shaded")
                    }
                ]
                Basic.AbstractButton {
                    id: material
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 101
                    Accessible.name: modelData.name
                    onClicked: AppearanceStore.setValue("material", modelData.id)
                    background: Rectangle {
                        radius: 7
                        color: "transparent"
                        border.color: material.visualFocus ? Appearance.text : Appearance.settings.material === material.modelData.id ? Appearance.accent : Appearance.outline
                    }
                    contentItem: Item {
                        Rectangle {
                            x: 8
                            y: 8
                            width: parent.width - 16
                            height: 45
                            radius: 3
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop {
                                    position: 0
                                    color: Qt.alpha(Appearance.stops[0], .6)
                                }
                                GradientStop {
                                    position: 1
                                    color: Qt.alpha(Appearance.stops[3], .6)
                                }
                            }
                            Rectangle {
                                x: 16
                                y: 9
                                width: parent.width * .57
                                height: 27
                                radius: 3
                                color: material.modelData.id === "light" ? "#e1e8ee" : Qt.alpha(Appearance.surface, material.modelData.id === "glass" ? .65 : 1)
                            }
                            Rectangle {
                                anchors.right: parent.right
                                anchors.rightMargin: 9
                                y: 20
                                width: 28
                                height: 19
                                radius: 3
                                color: Qt.alpha(Appearance.card, .7)
                                border.color: Appearance.outline
                            }
                        }
                        LookText {
                            x: 8
                            y: 63
                            text: material.modelData.name
                            size: 10
                        }
                        LookText {
                            x: 8
                            y: 79
                            width: parent.width - 16
                            text: material.modelData.description
                            size: 7
                            muted: true
                        }
                    }
                }
            }
        }
        LookToggle {
            Layout.fillWidth: true
            setting: "glow"
            label: qsTr("Edge glow")
            description: qsTr("A soft halo around focused surfaces.")
        }
    }
    LookCard {
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.fillHeight: true
        Heading {
            title: qsTr("Shape & spacing")
        }
        LookField {
            Layout.fillWidth: true
            setting: "density"
            label: qsTr("Density")
            choices: [
                {
                    value: "comfortable",
                    label: qsTr("Comfortable")
                },
                {
                    value: "compact",
                    label: qsTr("Compact")
                }
            ]
        }
        LookRange {
            Layout.fillWidth: true
            setting: "radius"
            label: qsTr("Corner radius")
            minimum: 4
        }
        LookRange {
            Layout.fillWidth: true
            setting: "gap"
            label: qsTr("Window gaps")
        }
    }
    LookCard {
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.fillHeight: true
        Heading {
            title: qsTr("Type & motion")
        }
        LookField {
            Layout.fillWidth: true
            setting: "uiFont"
            label: qsTr("Interface font")
            choices: [
                {
                    value: "",
                    label: qsTr("Phosphor default")
                }
            ].concat(AppearanceLibrary.fonts.map(f => ({
                        value: f,
                        label: f
                    })))
        }
        LookField {
            Layout.fillWidth: true
            setting: "monoFont"
            label: qsTr("Numbers & time")
            choices: [
                {
                    value: "",
                    label: qsTr("Phosphor default")
                }
            ].concat(AppearanceLibrary.fonts.map(f => ({
                        value: f,
                        label: f
                    })))
        }
        LookRange {
            Layout.fillWidth: true
            setting: "textScale"
            label: qsTr("Text size")
            minimum: 90
            maximum: 115
            unit: "%"
        }
        LookToggle {
            Layout.fillWidth: true
            setting: "motion"
            label: qsTr("Animations")
            description: qsTr("Gentle transitions between states.")
        }
    }
    LookCard {
        Layout.columnSpan: root.columns
        Layout.fillWidth: true
        Heading {
            title: qsTr("Other surfaces")
            subtitle: qsTr("Keep the same character throughout your desktop.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 24
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 14
                LookField {
                    Layout.fillWidth: true
                    setting: "visualizer"
                    label: qsTr("Media visualizer")
                    choices: [
                        {
                            value: "ribbon",
                            label: qsTr("Ribbon")
                        },
                        {
                            value: "bars",
                            label: qsTr("Bars")
                        },
                        {
                            value: "halo",
                            label: qsTr("Halo")
                        },
                        {
                            value: "off",
                            label: qsTr("Off")
                        }
                    ]
                }
                LookField {
                    Layout.fillWidth: true
                    setting: "notificationGrouping"
                    label: qsTr("Notification organization")
                    choices: [
                        {
                            value: "app",
                            label: qsTr("Group by application")
                        },
                        {
                            value: "time",
                            label: qsTr("Chronological")
                        }
                    ]
                }
                LookField {
                    Layout.fillWidth: true
                    setting: "lockLayout"
                    label: qsTr("Lock screen layout")
                    choices: [
                        {
                            value: "split",
                            label: qsTr("Clock beside unlock card")
                        },
                        {
                            value: "centered",
                            label: qsTr("Centered")
                        }
                    ]
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 25
                LookToggle {
                    Layout.fillWidth: true
                    setting: "lockMedia"
                    label: qsTr("Media on lock screen")
                    description: qsTr("Show track information while locked.")
                }
                LookToggle {
                    Layout.fillWidth: true
                    setting: "notificationPreviews"
                    label: qsTr("Notification previews")
                    description: qsTr("Show message text and pictures.")
                }
                LookToggle {
                    Layout.fillWidth: true
                    setting: "lockNotifications"
                    label: qsTr("Lock screen notification count")
                    description: qsTr("Keep message content private.")
                }
            }
        }
    }
    LookCard {
        Layout.columnSpan: root.columns
        Layout.fillWidth: true
        Heading {
            title: qsTr("Surface effects")
            subtitle: qsTr("Optional details for a more expressive desktop.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 24
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                LookField {
                    Layout.fillWidth: true
                    setting: "surfaceEffect"
                    label: qsTr("Effect pack")
                    choices: [
                        {
                            value: "none",
                            label: qsTr("None · clean surfaces")
                        },
                        {
                            value: "glass",
                            label: qsTr("Phosphor Glass · soft sweep")
                        },
                        {
                            value: "motes",
                            label: qsTr("Phosphor Motes · drifting light")
                        }
                    ]
                    onChosen: value => AppearanceStore.setValue("surfacePacks", value !== "none")
                }
                LookText {
                    text: qsTr("Default effects follow your palette. Per-surface choices in PlasmaZones Decorations take priority.")
                    size: 9
                    muted: true
                    Layout.fillWidth: true
                }
            }
            LookToggle {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                setting: "desktopStyle"
                label: qsTr("Match desktop windows")
                description: qsTr("Use the shell’s frame colors and corners.")
            }
        }
    }
}
