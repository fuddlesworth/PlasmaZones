// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations → Motion Sets — coordinated theme management.
 *
 * A motion set is a snapshot of every per-event override active at a
 * given moment, persisted as a single JSON ProfileTree blob under
 * `~/.local/share/plasmazones/motionsets/<slug>.json`. Applying a set
 * merges its overrides into the user's profiles dir (user files);
 * paths not in the set are preserved.
 *
 * Saving from the current state captures only path-named override
 * files — user presets in the same dir are intentionally excluded so
 * a set is portable and self-contained.
 */
Flickable {
    id: root

    // Loaded from a Q_INVOKABLE; the Connections block below manually
    // refreshes it on motionSetsChanged. See AnimationEventCard.qml's
    // shaderCombo for the same pattern (Q_INVOKABLE results aren't
    // reactive across the QML binding boundary).
    property var motionSetsList: settingsController.animationsPage.availableMotionSets()
    property bool _saving: false

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onMotionSetsChanged() {
            root.motionSetsList = settingsController.animationsPage.availableMotionSets();
            root._saving = false;
        }

        target: settingsController.animationsPage
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: true
            text: i18n("Motion sets bundle the current per-event overrides as one JSON file you can re-apply or share. Applying a set merges into your existing overrides — paths not in the set are kept as-is.")
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Save current state")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18n("Capture every per-event override file as a named motion set.")
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    TextField {
                        id: nameField

                        Layout.fillWidth: true
                        placeholderText: i18n("Set name…")
                        Accessible.name: i18n("Motion set name")
                    }

                    TextField {
                        id: descField

                        Layout.fillWidth: true
                        placeholderText: i18n("Description (optional)…")
                        Accessible.name: i18n("Motion set description")
                    }

                    Button {
                        text: i18n("Save")
                        icon.name: "document-save"
                        enabled: nameField.text.trim().length > 0 && !root._saving
                        onClicked: {
                            root._saving = true;
                            var ok = settingsController.animationsPage.saveCurrentAsMotionSet(nameField.text.trim(), descField.text.trim());
                            if (ok) {
                                nameField.text = "";
                                descField.text = "";
                            } else {
                                root._saving = false;
                            }
                        }
                    }

                }

            }

        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Saved sets (%1)", root.motionSetsList.length)

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    visible: root.motionSetsList.length === 0
                    text: i18n("No motion sets saved yet.")
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                    Layout.fillWidth: true
                }

                Repeater {
                    model: root.motionSetsList

                    delegate: RowLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                text: modelData.name
                                font.weight: Font.DemiBold
                            }

                            Label {
                                visible: modelData.description && modelData.description.length > 0
                                text: modelData.description
                                color: Kirigami.Theme.disabledTextColor
                                font: Kirigami.Theme.smallFont
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }

                            Label {
                                text: i18np("%1 override", "%1 overrides", modelData.overrideCount)
                                color: Kirigami.Theme.disabledTextColor
                                font: Kirigami.Theme.smallFont
                            }

                        }

                        Button {
                            text: i18n("Apply")
                            icon.name: "dialog-ok-apply"
                            Accessible.name: i18n("Apply motion set %1", modelData.name)
                            onClicked: settingsController.animationsPage.applyMotionSet(modelData.name)
                        }

                        Button {
                            icon.name: "edit-delete"
                            display: AbstractButton.IconOnly
                            ToolTip.text: i18n("Delete set")
                            ToolTip.visible: hovered
                            Accessible.name: i18n("Delete motion set %1", modelData.name)
                            onClicked: settingsController.animationsPage.removeMotionSet(modelData.name)
                        }

                    }

                }

            }

        }

    }

}
