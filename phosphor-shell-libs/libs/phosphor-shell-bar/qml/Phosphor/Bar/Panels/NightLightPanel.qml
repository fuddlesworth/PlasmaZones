// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Basic
import Phosphor.Theme
import Phosphor.Widgets

QuickDetailFrame {
    id: root
    required property var controller
    title: i18n("Night light")
    footerText: i18n("Display color temperature")
    footerIcon: "brightness-high"
    readonly property bool available: controller.nightLightAvailable
    readonly property bool pending: controller.nightLightPending
    readonly property string statusText: !controller.nightLightEnabled ? i18n("Off") : controller.nightLightPaused ? i18n("Paused") : controller.nightLightInhibited ? i18n("Inhibited") : controller.nightLightRunning ? i18n("On") : i18n("Scheduled")
    readonly property var schedules: [
        {
            id: "automatic",
            title: i18n("System day/night schedule"),
            description: i18n("Follow the daylight schedule configured for your desktop."),
            icon: "weather-clear"
        },
        {
            id: "custom",
            title: i18n("Custom hours"),
            description: i18n("Choose when warmer colors begin and end."),
            icon: "appointment-new"
        },
        {
            id: "manual",
            title: i18n("Manual"),
            description: i18n("Turn night light on whenever you want."),
            icon: "system-shutdown"
        }
    ]
    component TimeField: Basic.TextField {
        id: field
        padding: Tokens.spacing_m
        color: enabled ? Appearance.text : Appearance.muted
        placeholderTextColor: Appearance.muted
        selectionColor: Appearance.accent
        selectedTextColor: Appearance.surface
        font.family: Tokens.font_family_mono
        font.pixelSize: Tokens.font_size_body_l
        placeholderText: i18n("HH:mm")
        inputMethodHints: Qt.ImhTime
        validator: RegularExpressionValidator {
            regularExpression: /([01][0-9]|2[0-3]):[0-5][0-9]/
        }
        background: Rectangle {
            radius: Tokens.radius_s
            color: Appearance.recess
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? Appearance.accent : Appearance.outline
        }
    }
    headerAction: Component {
        DetailSwitch {
            objectName: "nightLightToggle"
            on: root.controller.nightLightEnabled
            enabled: root.available && !root.pending
            Accessible.name: i18n("Night light")
            onClicked: root.controller.setNightLightEnabled(!on)
        }
    }

    DetailNotice {
        text: root.controller.nightLightError
        error: true
    }
    DetailEmptyState {
        visible: !root.available
        title: i18n("Night light is unavailable")
        description: i18n("Reconnect to the display service to adjust screen warmth.")
        iconName: "brightness-high"
        actionText: i18n("Try again")
        onActivated: root.controller.refreshNightLight()
    }
    Column {
        visible: root.available
        width: parent.width
        spacing: Tokens.spacing_l
        DetailCard {
            title: root.controller.nightLightRunning ? i18n("A softer evening.") : i18n("Warmth when you need it.")
            description: root.controller.nightLightSchedule === "automatic" ? i18n("Follows the system day/night schedule.") : root.controller.nightLightSchedule === "custom" ? i18n("%1 to %2, every day.").arg(root.controller.nightLightEvening).arg(root.controller.nightLightMorning) : i18n("Follows your on and off control.")
            iconName: "brightness-high"
            status: root.statusText
            tone: 2
            RowLayout {
                width: parent.width
                spacing: Tokens.spacing_s
                DetailText {
                    Layout.fillWidth: true
                    text: i18n("Color temperature")
                }
                DetailText {
                    text: i18n("%1 K").arg(Math.round(temperature.value))
                    muted: true
                    font.family: Tokens.font_family_mono
                }
            }
            Basic.Slider {
                id: temperature
                objectName: "nightLightTemperature"
                width: parent.width
                implicitHeight: 30
                padding: 0
                from: 1000
                to: 6500
                stepSize: 100
                value: root.controller.nightLightTemperature
                enabled: root.available && !root.pending
                Accessible.name: i18n("Color temperature")
                onPressedChanged: {
                    if (!pressed && value !== root.controller.nightLightTemperature)
                        root.controller.setNightLightTemperature(Math.round(value));
                }
                onMoved: {
                    if (!pressed)
                        root.controller.setNightLightTemperature(Math.round(value));
                }
                background: Rectangle {
                    y: (temperature.height - height) / 2
                    width: temperature.availableWidth
                    height: 10
                    radius: 5
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0
                            color: Appearance.stops[2]
                        }
                        GradientStop {
                            position: 1
                            color: Appearance.stops[0]
                        }
                    }
                }
                handle: Rectangle {
                    x: temperature.leftPadding + temperature.visualPosition * (temperature.availableWidth - width)
                    y: (temperature.height - height) / 2
                    width: 7
                    height: 22
                    radius: 3
                    color: Appearance.text
                    border.width: temperature.visualFocus ? 2 : 0
                    border.color: Appearance.stops[1]
                }
            }
            RowLayout {
                width: parent.width
                spacing: Tokens.spacing_s
                DetailText {
                    Layout.fillWidth: true
                    text: i18n("Warmer")
                    muted: true
                    font.pixelSize: Tokens.font_size_label_s
                }
                DetailText {
                    text: i18n("Cooler")
                    muted: true
                    font.pixelSize: Tokens.font_size_label_s
                }
            }
        }
        DetailNotice {
            text: root.controller.nightLightInhibited && !root.controller.nightLightPaused ? i18n("Another application has temporarily paused night light.") : ""
        }
        DetailSectionHeading {
            title: i18n("Schedule")
        }
        Column {
            width: parent.width
            spacing: Tokens.spacing_s
            Repeater {
                id: scheduleChoices
                model: root.schedules
                delegate: DetailDeviceRow {
                    id: scheduleRow
                    required property var modelData
                    required property int index
                    objectName: "nightLightSchedule-" + modelData.id
                    title: modelData.title
                    subtitle: modelData.description
                    iconName: modelData.icon
                    radio: true
                    selected: root.controller.nightLightSchedule === modelData.id
                    enabled: !root.pending && (modelData.id === "manual" || root.controller.nightLightScheduleAvailable)
                    onClicked: root.controller.setNightLightSchedule(modelData.id)
                    Keys.onUpPressed: event => {
                        if (index > 0)
                            scheduleChoices.itemAt(index - 1).forceActiveFocus();
                        event.accepted = true;
                    }
                    Keys.onDownPressed: event => {
                        if (index + 1 < root.schedules.length)
                            scheduleChoices.itemAt(index + 1).forceActiveFocus();
                        event.accepted = true;
                    }
                }
            }
        }
        DetailText {
            width: parent.width
            text: root.controller.nightLightScheduleAvailable ? i18n("Schedule changes also update the desktop's shared day/night schedule.") : i18n("The system schedule is unavailable. You can still control night light manually.")
            muted: true
            font.pixelSize: Tokens.font_size_label_s
        }
        DetailCard {
            visible: root.controller.nightLightSchedule === "custom"
            compact: true
            title: i18n("Custom hours")
            RowLayout {
                width: parent.width
                spacing: Tokens.spacing_m
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.spacing_s
                    DetailText {
                        text: i18n("Starts")
                    }
                    TimeField {
                        id: evening
                        objectName: "nightLightEvening"
                        Layout.fillWidth: true
                        text: root.controller.nightLightEvening
                        Accessible.name: i18n("Night light start time")
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.spacing_s
                    DetailText {
                        text: i18n("Ends")
                    }
                    TimeField {
                        id: morning
                        objectName: "nightLightMorning"
                        Layout.fillWidth: true
                        text: root.controller.nightLightMorning
                        Accessible.name: i18n("Night light end time")
                    }
                }
            }
            DetailText {
                visible: evening.acceptableInput && morning.acceptableInput && evening.text === morning.text
                width: parent.width
                text: i18n("Choose different start and end times.")
                muted: true
                font.pixelSize: Tokens.font_size_label_s
            }
            ShellButton {
                objectName: "nightLightSaveHours"
                text: i18n("Save hours")
                enabled: !root.pending && root.controller.nightLightScheduleAvailable && evening.acceptableInput && morning.acceptableInput && evening.text !== morning.text
                onClicked: root.controller.setNightLightTimes(morning.text, evening.text)
            }
        }
        ShellButton {
            objectName: "nightLightPause"
            visible: root.controller.nightLightEnabled
            text: root.controller.nightLightPaused ? i18n("Resume night light") : i18n("Pause night light")
            enabled: !root.pending
            onClicked: root.controller.toggleNightLightPause()
        }
    }
}
