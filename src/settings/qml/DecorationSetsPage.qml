// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Decoration → Sets — coordinated decoration theme management,
 *        the Motion Sets twin for surface-pack decoration.
 *
 * A decoration set is a snapshot of the decoration profile tree (the
 * baseline plus every per-surface override) persisted as one JSON file
 * under `~/.local/share/plasmazones/decorationsets/<slug>.json`.
 * Applying a set merges: surfaces it covers are replaced, surfaces it
 * does not cover keep their current chains.
 */
SettingsFlickable {
    id: root

    // Loaded from a Q_INVOKABLE; refreshed manually on
    // decorationSetsChanged (Q_INVOKABLE results are not reactive across
    // the QML binding boundary — same pattern as the Motion Sets page).
    property var setsList: settingsController.decorationPage.availableDecorationSets()

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onDecorationSetsChanged() {
            root.setsList = settingsController.decorationPage.availableDecorationSets();
        }

        function onToastRequested(text) {
            if (typeof window !== "undefined" && window && window.showToast)
                window.showToast(text);
        }

        target: settingsController.decorationPage
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: true
            text: i18n("Decoration sets bundle your per-surface pack chains into one shareable JSON file. Applying a set merges into your current decoration. Surfaces it doesn't cover are left unchanged.")
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Save current state")
            searchAnchor: "saveDecorationSet"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Capture the baseline and every per-surface override as a named decoration set.")
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    TextField {
                        id: nameField

                        Layout.fillWidth: true
                        placeholderText: i18n("Set name…")
                        Accessible.name: i18n("Decoration set name")
                    }

                    TextField {
                        id: descField

                        Layout.fillWidth: true
                        placeholderText: i18n("Description (optional)…")
                        Accessible.name: i18n("Decoration set description")
                    }

                    Kirigami.InlineMessage {
                        id: saveStatus

                        Layout.fillWidth: true
                        type: Kirigami.MessageType.Error
                        showCloseButton: true
                    }

                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: i18n("Save")
                        icon.name: "document-save"
                        Accessible.name: i18n("Save decoration set")
                        enabled: nameField.text.trim().length > 0
                        onClicked: {
                            saveStatus.visible = false;
                            var ok = settingsController.decorationPage.saveCurrentAsDecorationSet(nameField.text.trim(), descField.text.trim());
                            if (ok) {
                                nameField.text = "";
                                descField.text = "";
                            } else {
                                saveStatus.text = i18n("Could not save decoration set. Check that the name is unique and that ~/.local/share/plasmazones is writable.");
                                saveStatus.visible = true;
                            }
                        }
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Saved sets (%1)", root.setsList.length)
            searchAnchor: "savedDecorationSets"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    visible: root.setsList.length === 0
                    text: i18n("No decoration sets saved yet.")
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                }

                Repeater {
                    model: root.setsList

                    delegate: RowLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
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
                                text: i18np("%n surface", "%n surfaces", modelData.overrideCount)
                                color: Kirigami.Theme.disabledTextColor
                                font: Kirigami.Theme.smallFont
                            }
                        }

                        Button {
                            text: i18n("Apply")
                            icon.name: "dialog-ok-apply"
                            Accessible.name: i18n("Apply decoration set %1", modelData.name)
                            onClicked: applyConfirm.open()
                        }

                        Button {
                            icon.name: "edit-delete"
                            display: AbstractButton.IconOnly
                            ToolTip.text: i18n("Delete set")
                            ToolTip.visible: hovered
                            Accessible.name: i18n("Delete decoration set %1", modelData.name)
                            onClicked: deleteConfirm.open()
                        }

                        Kirigami.PromptDialog {
                            id: applyConfirm

                            title: i18n("Apply decoration set?")
                            subtitle: i18n("\"%1\" will replace the decoration on every surface it covers.", modelData.name)
                            standardButtons: Kirigami.Dialog.Apply | Kirigami.Dialog.Cancel
                            onApplied: settingsController.decorationPage.applyDecorationSet(modelData.name)
                        }

                        Kirigami.PromptDialog {
                            id: deleteConfirm

                            title: i18n("Delete decoration set?")
                            subtitle: i18n("\"%1\" will be permanently removed.", modelData.name)
                            standardButtons: Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
                            onDiscarded: {
                                settingsController.decorationPage.removeDecorationSet(modelData.name);
                                deleteConfirm.close();
                            }
                        }
                    }
                }
            }
        }
    }
}
