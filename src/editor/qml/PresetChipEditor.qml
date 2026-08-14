// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ThemeHelpers.js" as Theme
import org.kde.kirigami as Kirigami

/**
 * @brief Chip-row editor for a preset fraction vocabulary
 *
 * Shows each preset stop as a removable chip labelled in percent, with a
 * percent spin and an add button to append new stops. Clicking a chip's
 * label edits it in place (Enter or focus-out commits, Escape cancels). Stateless view over
 * @ref values: every edit is emitted whole through @ref edited and the chips
 * re-render from whatever the model stores, so untouched stops keep their
 * exact stored fraction (a bundled 0.33333 stays 0.33333 unless the user
 * removes it). Appends insert sorted, matching the store's normalization,
 * and near-duplicates within the dedupe epsilon are refused up front.
 */
ColumnLayout {
    id: chipEditor

    /// The stops, as work-area fractions 0..1.
    property var values: []
    property int minPercent: 5
    property int maxCount: 16
    property real dedupeEpsilon: 0.01
    /// Names the vocabulary for assistive tech ("Width presets").
    property string accessibleLabel: ""

    /// The full replacement list after any chip edit.
    signal edited(var newValues)

    readonly property int count: values ? values.length : 0
    readonly property bool full: count >= maxCount

    spacing: Kirigami.Units.smallSpacing

    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: chipEditor.values

            delegate: Rectangle {
                id: chip

                required property real modelData
                required property int index

                // In-place retype state. Committing emits the whole list, so
                // the delegate rebuild that follows lands after the edit is
                // done, the same commit-after-gesture rule the strip canvas
                // follows.
                property bool editing: false

                function commitEdit() {
                    if (!chip.editing)
                        return;

                    chip.editing = false;
                    const percent = parseInt(editField.text);
                    if (!isFinite(percent))
                        return;

                    const fraction = Math.max(chipEditor.minPercent, Math.min(100, percent)) / 100;
                    for (let i = 0; i < chipEditor.values.length; i++) {
                        if (i !== chip.index && Math.abs(chipEditor.values[i] - fraction) < chipEditor.dedupeEpsilon)
                            return; // Another stop already holds this size.
                    }
                    const next = chipEditor.values.slice();
                    next[chip.index] = fraction;
                    next.sort((a, b) => a - b);
                    chipEditor.edited(next);
                }

                implicitWidth: chipContent.implicitWidth + Kirigami.Units.largeSpacing
                implicitHeight: Kirigami.Units.gridUnit * 1.6
                radius: height / 2
                color: chip.editing ? Theme.withAlpha(Kirigami.Theme.highlightColor, 0.15) : Theme.withAlpha(Kirigami.Theme.alternateBackgroundColor, 0.8)
                border.width: 1
                border.color: chip.editing ? Kirigami.Theme.focusColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

                RowLayout {
                    id: chipContent

                    anchors.centerIn: parent
                    spacing: 0

                    Label {
                        visible: !chip.editing
                        text: i18n("%1%", Math.round(chip.modelData * 100))
                        leftPadding: Kirigami.Units.smallSpacing

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.IBeamCursor
                            onClicked: {
                                editField.text = String(Math.round(chip.modelData * 100));
                                chip.editing = true;
                                editField.forceActiveFocus();
                                editField.selectAll();
                            }
                        }

                        ToolTip.visible: chipHover.hovered
                        ToolTip.text: i18n("Click to edit this preset")

                        HoverHandler {
                            id: chipHover
                        }
                    }

                    TextField {
                        id: editField

                        visible: chip.editing
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                        horizontalAlignment: TextInput.AlignHCenter
                        validator: IntValidator {
                            bottom: chipEditor.minPercent
                            top: 100
                        }
                        Accessible.name: i18n("Edit preset percentage in %1", chipEditor.accessibleLabel)
                        // Escape cancels without committing; Enter and
                        // focus-out both commit (commitEdit latches on
                        // chip.editing so the pair cannot double-fire).
                        Keys.onEscapePressed: chip.editing = false
                        onAccepted: chip.commitEdit()
                        onActiveFocusChanged: {
                            if (!activeFocus)
                                chip.commitEdit();
                        }
                    }

                    // Focus-out alone cannot close the edit: QML only moves
                    // active focus when another item TAKES it, and most of
                    // the editor (canvas, panel dead space) takes none. So
                    // while editing, park a catcher on the window overlay:
                    // any press outside the field commits, and accepted =
                    // false lets the same press continue to whatever it hit.
                    // The commit is deferred a tick because it triggers the
                    // delegate rebuild, and tearing this item down inside
                    // its own press handler is the popup-UAF shape the
                    // shared context menu comment warns about.
                    MouseArea {
                        parent: chip.editing ? chipEditor.Overlay.overlay : null
                        anchors.fill: parent ? parent : undefined
                        enabled: chip.editing
                        z: 1000
                        onPressed: mouse => {
                            mouse.accepted = false;
                            const p = mapToItem(editField, mouse.x, mouse.y);
                            if (p.x >= 0 && p.y >= 0 && p.x <= editField.width && p.y <= editField.height)
                                return; // A click INTO the field must not commit.

                            Qt.callLater(chip.commitEdit);
                        }
                    }

                    ToolButton {
                        visible: !chip.editing
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 1.4
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 1.4
                        icon.name: "window-close-symbolic"
                        icon.width: Kirigami.Units.iconSizes.small * 0.75
                        icon.height: Kirigami.Units.iconSizes.small * 0.75
                        Accessible.name: i18n("Remove %1% from %2", Math.round(chip.modelData * 100), chipEditor.accessibleLabel)
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Remove this preset")
                        onClicked: {
                            const next = chipEditor.values.slice();
                            next.splice(chip.index, 1);
                            chipEditor.edited(next);
                        }
                    }
                }
            }
        }

        // Add controls ride the flow so they sit right after the last chip.
        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            SpinBox {
                id: addSpin

                from: chipEditor.minPercent
                to: 100
                value: 50
                editable: true
                enabled: !chipEditor.full
                textFromValue: (value, locale) => i18n("%1%", value)
                valueFromText: (text, locale) => parseInt(text)
                Accessible.name: i18n("New preset percentage for %1", chipEditor.accessibleLabel)
            }

            ToolButton {
                icon.name: "list-add"
                enabled: !chipEditor.full
                Accessible.name: i18n("Add preset to %1", chipEditor.accessibleLabel)
                ToolTip.visible: hovered
                ToolTip.text: chipEditor.full ? i18n("This list can hold at most %1 presets", chipEditor.maxCount) : i18n("Add this size as a preset")
                onClicked: {
                    const fraction = addSpin.value / 100;
                    const next = chipEditor.values.slice();
                    for (let i = 0; i < next.length; i++) {
                        if (Math.abs(next[i] - fraction) < chipEditor.dedupeEpsilon)
                            return; // Already a stop; the store would drop it anyway.
                    }
                    next.push(fraction);
                    next.sort((a, b) => a - b);
                    chipEditor.edited(next);
                }
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: chipEditor.count === 0
        wrapMode: Text.WordWrap
        opacity: 0.7
        font: Kirigami.Theme.smallFont
        text: i18n("No presets yet. Pick a size and add it.")
    }
}
