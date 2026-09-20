// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Basic.Popup {
    id: root
    required property var controller
    property string mode: "save"
    property var preset: ({})
    property bool includeWallpaper: false
    property bool includeBar: false
    property bool saveCopy: false
    modal: true
    focus: true
    padding: 26
    width: Math.min(460, parent.width - 40)
    x: (parent.width - width) / 2
    y: Math.max(20, (parent.height - height) / 2)
    closePolicy: Basic.Popup.NoAutoClose
    Component.onCompleted: if (controller.closePending)
        show("close", {})
    function show(kind, value) {
        mode = kind;
        preset = value || {};
        includeWallpaper = false;
        includeBar = false;
        saveCopy = false;
        name.text = kind === "import" ? preset.name || "" : "";
        open();
        Qt.callLater(() => {
            if (mode === "save" || mode === "import")
                name.forceActiveFocus();
            else
                cancel.forceActiveFocus();
        });
    }
    function dismiss() {
        controller.keepEditing();
        close();
    }
    Connections {
        target: root.controller
        function onClosePendingChanged() {
            if (root.controller.closePending)
                root.show("close", {});
            else if (root.mode === "close")
                root.close();
        }
    }
    background: ShellSurface {
        accented: true
    }
    Basic.Overlay.modal: Rectangle {
        color: Qt.alpha(Appearance.recess, .6)
    }
    contentItem: ColumnLayout {
        spacing: 18
        Keys.onEscapePressed: root.dismiss()
        LookText {
            Layout.fillWidth: true
            size: 21
            text: root.mode === "close" ? qsTr("Keep this look?") : root.mode === "delete" ? qsTr("Delete %1?").arg(root.preset.name) : root.mode === "save" ? qsTr("Save your look") : root.mode === "import" ? qsTr("Import a look") : qsTr("Preview %1").arg(root.preset.name)
        }
        LookText {
            Layout.fillWidth: true
            muted: true
            size: 11
            lineHeight: 1.5
            text: root.mode === "close" ? qsTr("Your changes are still a preview. Apply them or return to your saved look.") : root.mode === "delete" ? qsTr("This removes the saved preset. Your current appearance stays as it is.") : root.mode === "save" ? qsTr("Keep these colors, material and spacing for another day.") : qsTr("Style is included. Choose whether to bring wallpapers and the bar layout too. Apply when it feels right.")
        }
        Basic.TextField {
            id: name
            Layout.fillWidth: true
            visible: root.mode === "save" || root.mode === "import"
            implicitHeight: 38
            padding: 11
            maximumLength: 60
            color: Appearance.text
            placeholderTextColor: Appearance.muted
            placeholderText: qsTr("Name your look")
            Accessible.name: qsTr("Preset name")
            font.family: Tokens.font_family_ui
            font.pixelSize: Math.round((12) * Appearance.textScale)
            selectByMouse: true
            background: Rectangle {
                radius: 7
                color: Appearance.recess
                border.color: name.activeFocus ? Appearance.accent : Appearance.outline
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 18
            visible: root.mode === "save" || root.mode === "preset" || root.mode === "import"
            LookToggle {
                Layout.fillWidth: true
                label: qsTr("Include wallpapers")
                description: qsTr("Images and placement for each display.")
                enabled: root.mode === "save" || !!root.preset.settings?.wallpapers
                checked: root.includeWallpaper
                onToggled: checked => root.includeWallpaper = checked
            }
            LookToggle {
                Layout.fillWidth: true
                label: qsTr("Include bar layout")
                description: qsTr("Widget positions, screen edge and inset.")
                enabled: root.mode === "save" || !!root.preset.settings?.barLayout
                checked: root.includeBar
                onToggled: checked => root.includeBar = checked
            }
            LookToggle {
                Layout.fillWidth: true
                visible: root.mode === "import"
                label: qsTr("Save a copy to my presets")
                checked: root.saveCopy
                onToggled: checked => root.saveCopy = checked
            }
        }
        LookText {
            Layout.fillWidth: true
            visible: !!text
            text: AppearanceLibrary.error || AppearanceStore.error
            size: 10
            color: Appearance.light ? "#a52b34" : "#f5a0a5"
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ShellButton {
                id: cancel
                text: root.mode === "close" ? qsTr("Keep editing") : qsTr("Cancel")
                flat: true
                onClicked: root.dismiss()
            }
            Item {
                Layout.fillWidth: true
            }
            ShellButton {
                visible: root.mode === "close"
                text: qsTr("Discard")
                onClicked: {
                    root.controller.discard();
                    root.close();
                }
            }
            ShellButton {
                highlighted: true
                implicitHeight: 36
                text: root.mode === "close" ? qsTr("Apply & close") : root.mode === "delete" ? qsTr("Delete preset") : root.mode === "save" ? qsTr("Save preset") : qsTr("Preview look")
                enabled: !AppearanceLibrary.busy && ((root.mode !== "save" && (root.mode !== "import" || !root.saveCopy)) || name.text.trim().length > 0)
                onClicked: {
                    let success = false;
                    if (root.mode === "close")
                        success = root.controller.apply(true);
                    else if (root.mode === "delete")
                        success = AppearanceLibrary.removePreset(root.preset.id);
                    else if (root.mode === "save")
                        success = AppearanceLibrary.savePreset(name.text, root.includeWallpaper, root.includeBar);
                    else {
                        success = AppearanceLibrary.previewPreset(root.mode === "import" ? "imported" : root.preset.id, root.includeWallpaper, root.includeBar);
                        if (success && root.mode === "import" && root.saveCopy)
                            success = AppearanceLibrary.saveImported(name.text);
                    }
                    if (success)
                        root.close();
                }
            }
        }
    }
}
