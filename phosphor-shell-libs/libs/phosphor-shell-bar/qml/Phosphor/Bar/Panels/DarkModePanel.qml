// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

QuickDetailFrame {
    id: root
    required property var controller
    signal appearanceRequested(string page)
    title: i18n("Dark mode")
    footerIcon: "preferences-desktop-theme"
    footerText: i18n("Your colors and wallpaper stay with you.")
    readonly property var choices: [
        {
            mode: "light",
            title: i18n("Light"),
            description: i18n("A bright surface for the shell."),
            icon: "weather-clear"
        },
        {
            mode: "dark",
            title: i18n("Dark"),
            description: i18n("Use your chosen glass or solid material."),
            icon: "weather-clear-night"
        },
        {
            mode: "system",
            title: i18n("Follow system"),
            description: i18n("Use the system color preference."),
            icon: "computer"
        }
    ]
    function choose(index): void {
        const choice = root.choices[index];
        if (choice.mode !== "system" || root.controller.systemAvailable) {
            rows.itemAt(index).forceActiveFocus();
            root.controller.setMode(choice.mode);
        }
    }
    DetailNotice {
        text: root.controller.error
        error: true
    }
    DetailNotice {
        text: root.controller.previewActive ? i18n("Appearance is open. Apply these preview changes there to save them.") : ""
    }
    DetailCard {
        title: root.controller.effectiveDark ? i18n("A quieter glow.") : i18n("A little more light.")
        description: i18n("Choose how Phosphor looks or let it follow your system preference.")
        iconName: root.controller.effectiveDark ? "weather-clear-night" : "weather-clear"
        status: root.controller.mode === "system" ? i18n("Following system") : root.controller.effectiveDark ? i18n("Dark") : i18n("Light")
    }
    DetailList {
        Accessible.role: Accessible.Grouping
        Accessible.name: i18n("Color mode")
        Repeater {
            id: rows
            model: root.choices
            delegate: DetailDeviceRow {
                required property int index
                required property var modelData
                objectName: "colorMode_" + modelData.mode
                grouped: true
                radio: true
                title: modelData.title
                subtitle: modelData.mode !== "system" ? modelData.description : !root.controller.systemAvailable ? i18n("The system preference is unavailable.") : root.controller.systemDark ? i18n("Dark when following system.") : i18n("Light when following system.")
                iconName: modelData.icon
                selected: root.controller.mode === modelData.mode
                enabled: modelData.mode !== "system" || root.controller.systemAvailable
                activeFocusOnTab: activeFocus || selected || root.controller.mode === "system" && !root.controller.systemAvailable && index === 0
                onClicked: root.choose(index)
                Keys.onDownPressed: event => {
                    root.choose((index + 1) % (root.controller.systemAvailable ? 3 : 2));
                    event.accepted = true;
                }
                Keys.onUpPressed: event => {
                    const count = root.controller.systemAvailable ? 3 : 2;
                    root.choose((index + count - 1) % count);
                    event.accepted = true;
                }
            }
        }
    }
    ShellButton {
        visible: !root.controller.systemAvailable
        text: i18n("Check system preference")
        iconName: "view-refresh"
        flat: true
        onClicked: root.controller.refresh()
    }
    DetailDeviceRow {
        title: i18n("Style settings")
        subtitle: i18n("Adjust material, colors and spacing in Appearance.")
        iconName: "preferences-desktop-theme"
        onClicked: root.appearanceRequested("style")
    }
}
