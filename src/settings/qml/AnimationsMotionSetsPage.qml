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
SettingsFlickable {
    id: root

    // Loaded from a Q_INVOKABLE; the Connections block below manually
    // refreshes it on motionSetsChanged. See AnimationEventCard.qml's
    // shaderCombo for the same pattern (Q_INVOKABLE results aren't
    // reactive across the QML binding boundary).
    property var motionSetsList: settingsController.animationsPage.availableMotionSets()
    // QVariantList from C++
    property bool _saving: false

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onMotionSetsChanged() {
            root.motionSetsList = settingsController.animationsPage.availableMotionSets();
            root._saving = false;
        }

        // Surface controller-emitted toast requests (e.g. an apply/save
        // refused mid-discard) through the shell `window.showToast`.
        // Without this the controller's diagnostic vanishes and the
        // user sees an unexplained no-op.
        function onToastRequested(text) {
            if (window && window.showToast)
                window.showToast(text);
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
            text: i18n("Motion sets bundle your per-event overrides into one shareable JSON file. Applying a set merges into your current overrides. Paths it doesn't cover are left unchanged.")
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Save current state")
            searchAnchor: "saveMotionSet"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Capture every per-event override file as a named motion set.")
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                }

                // Stack the name + description fields vertically; a
                // side-by-side RowLayout split them 50/50 at every window
                // width which made the description column unreadably narrow
                // on the typical settings pane (~600 px). The Save button
                // sits at the bottom-right of the stack so the focus order
                // matches typeflow (name → description → save).
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
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
                        Accessible.name: i18n("Save motion set")
                        enabled: nameField.text.trim().length > 0 && !root._saving
                        onClicked: {
                            root._saving = true;
                            saveStatus.visible = false;
                            var ok = settingsController.animationsPage.saveCurrentAsMotionSet(nameField.text.trim(), descField.text.trim());
                            if (ok) {
                                nameField.text = "";
                                descField.text = "";
                            } else {
                                // The C++ side logs the reason via qCWarning; surface
                                // a generic banner so the user sees that something
                                // went wrong instead of staring at an inert form.
                                saveStatus.text = i18n("Could not save motion set. Check that the name is unique and that ~/.local/share/plasmazones is writable.");
                                saveStatus.visible = true;
                            }
                            root._saving = false;
                        }
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Saved sets (%1)", root.motionSetsList.length)
            searchAnchor: "savedMotionSets"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    visible: root.motionSetsList.length === 0
                    text: i18n("No motion sets saved yet.")
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                }

                Repeater {
                    model: root.motionSetsList

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
                                text: i18np("%n override", "%n overrides", modelData.overrideCount)
                                color: Kirigami.Theme.disabledTextColor
                                font: Kirigami.Theme.smallFont
                            }
                        }

                        Button {
                            text: i18n("Apply")
                            icon.name: "dialog-ok-apply"
                            Accessible.name: i18n("Apply motion set %1", modelData.name)
                            onClicked: applyConfirm.open()
                        }

                        Button {
                            icon.name: "edit-delete"
                            display: AbstractButton.IconOnly
                            ToolTip.text: i18n("Delete set")
                            ToolTip.visible: hovered
                            Accessible.name: i18n("Delete motion set %1", modelData.name)
                            onClicked: deleteConfirm.open()
                        }

                        // Apply overwrites every per-event override file at
                        // once — the user wouldn't know which paths were
                        // touched after a misclick. Confirm before the
                        // batch write; the controller's snapshot store
                        // backs Discard if they then change their mind.
                        Kirigami.PromptDialog {
                            id: applyConfirm

                            title: i18n("Apply motion set?")
                            subtitle: i18n("\"%1\" will overwrite every per-event override matching its %2.", modelData.name, i18np("entry", "entries", modelData.overrideCount))
                            standardButtons: Kirigami.Dialog.Apply | Kirigami.Dialog.Cancel
                            onApplied: settingsController.animationsPage.applyMotionSet(modelData.name)
                        }

                        // Delete is permanent — the JSON file under
                        // motionsets/ is removed and only ~/.local backups
                        // would let the user recover.
                        Kirigami.PromptDialog {
                            id: deleteConfirm

                            title: i18n("Delete motion set?")
                            subtitle: i18n("\"%1\" will be permanently removed.", modelData.name)
                            standardButtons: Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
                            onDiscarded: {
                                settingsController.animationsPage.removeMotionSet(modelData.name);
                                deleteConfirm.close();
                            }
                        }
                    }
                }
            }
        }
    }
}
