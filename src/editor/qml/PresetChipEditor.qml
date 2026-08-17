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

                    // No-change bail, against the DISPLAYED percent rather
                    // than the stored fraction: the field opens rounded, so
                    // an untouched commit would rewrite a bundled 0.33333 as
                    // 0.33 — exactly the exact-stored-fraction contract the
                    // header promises to keep. It also keeps a plain
                    // click-away from rebuilding the whole chip row (the
                    // rebuild mid-click is what swallowed the next press).
                    if (percent === Math.round(chip.modelData * 100))
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
                // Keyboard focus on the label lights the chip border, so a
                // Tab stop is visible the way a focused field is.
                border.color: (chip.editing || chipLabel.activeFocus) ? Kirigami.Theme.focusColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

                RowLayout {
                    id: chipContent

                    anchors.centerIn: parent
                    spacing: 0

                    Label {
                        id: chipLabel

                        function startEdit() {
                            editField.text = String(Math.round(chip.modelData * 100));
                            chip.editing = true;
                            editField.forceActiveFocus();
                            editField.selectAll();
                        }

                        visible: !chip.editing
                        text: i18nc("@info preset percentage", "%1%", Math.round(chip.modelData * 100))
                        leftPadding: Kirigami.Units.smallSpacing
                        // Keyboard and assistive-tech route into edit mode:
                        // without these the chip's value is mouse-only and
                        // the sole recourse is delete-and-re-add.
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: i18nc("@action:button", "Edit the %1% preset in %2", Math.round(chip.modelData * 100), chipEditor.accessibleLabel)
                        Accessible.onPressAction: chipLabel.startEdit()
                        Keys.onReturnPressed: chipLabel.startEdit()
                        Keys.onEnterPressed: chipLabel.startEdit()
                        Keys.onSpacePressed: chipLabel.startEdit()

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.IBeamCursor
                            onClicked: chipLabel.startEdit()
                        }

                        ToolTip.visible: chipHover.hovered
                        ToolTip.text: i18nc("@info:tooltip", "Click to edit this preset")

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
                        Accessible.name: i18nc("@label:textbox", "Edit preset percentage in %1", chipEditor.accessibleLabel)
                        // Escape cancels without committing; Enter and
                        // focus-out both commit (commitEdit latches on
                        // chip.editing so the pair cannot double-fire, and
                        // Qt.callLater de-dupes by function reference).
                        // BOTH commits defer a tick, like the overlay catcher
                        // below: the commit triggers the delegate rebuild,
                        // and tearing this item down synchronously inside its
                        // own signal handler is the UAF shape the overlay
                        // comment describes.
                        Keys.onEscapePressed: chip.editing = false
                        onAccepted: Qt.callLater(chip.commitEdit)
                        onActiveFocusChanged: {
                            if (activeFocus)
                                return;
                            // Mid-gesture the overlay catcher owns the
                            // commit and fires it on RELEASE. Committing
                            // here too would land the Repeater rebuild
                            // between the press that stole the focus and its
                            // release, destroying the pressed delegate and
                            // swallowing that click (QQC2 buttons take focus
                            // on press, so this path fires exactly then).
                            // With no gesture running (Tab-away,
                            // programmatic focus) this is the sole commit.
                            if (catcherPoint.active)
                                return;
                            Qt.callLater(chip.commitEdit);
                        }
                    }

                    // Focus-out alone cannot close the edit: QML only moves
                    // active focus when another item TAKES it, and most of
                    // the editor (canvas, panel dead space) takes none. So
                    // while editing, park a catcher on the window overlay.
                    // A passive PointHandler rather than a MouseArea, for the
                    // release timing: the commit rebuilds the chip row, and a
                    // commit scheduled from the PRESS ran between that press
                    // and its release, destroying whichever delegate the
                    // click had landed on and swallowing its activation. The
                    // handler observes without grabbing (the press continues
                    // to whatever it hit) and commits only when the gesture
                    // ENDS outside the field. The commit still defers a tick:
                    // tearing this item down inside its own handler is the
                    // popup-UAF shape the shared context menu comment warns
                    // about.
                    Item {
                        id: outsideCatcher

                        property bool pressedOutside: false

                        parent: chip.editing ? chipEditor.Overlay.overlay : null
                        anchors.fill: parent ? parent : undefined
                        visible: chip.editing

                        PointHandler {
                            id: catcherPoint

                            enabled: chip.editing
                            onActiveChanged: {
                                if (active) {
                                    const p = outsideCatcher.mapToItem(editField, point.position.x, point.position.y);
                                    // A click INTO the field must not commit.
                                    outsideCatcher.pressedOutside = !(p.x >= 0 && p.y >= 0 && p.x <= editField.width && p.y <= editField.height);
                                    return;
                                }
                                if (outsideCatcher.pressedOutside) {
                                    outsideCatcher.pressedOutside = false;
                                    Qt.callLater(chip.commitEdit);
                                }
                            }
                        }
                    }

                    ToolButton {
                        visible: !chip.editing
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 1.4
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 1.4
                        icon.name: "window-close-symbolic"
                        icon.width: Kirigami.Units.iconSizes.small * 0.75
                        icon.height: Kirigami.Units.iconSizes.small * 0.75
                        Accessible.name: i18nc("@action:button", "Remove %1% from %2", Math.round(chip.modelData * 100), chipEditor.accessibleLabel)
                        ToolTip.visible: hovered
                        ToolTip.text: i18nc("@info:tooltip", "Remove this preset")
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
                textFromValue: (value, locale) => i18nc("@info preset percentage", "%1%", value)
                // Locale-aware, the twin of TemplatePropertyPanel's width
                // spin: parseInt reads only ASCII digits, so a locale that
                // writes its numbers any other way parsed as NaN. The percent
                // sign textFromValue adds is stripped first, and text the
                // locale cannot parse keeps the current value.
                valueFromText: (text, locale) => {
                    const trimmed = text.replace(locale.percent, "").trim();
                    try {
                        return Math.round(Number.fromLocaleString(locale, trimmed));
                    } catch (e) {
                        return addSpin.value;
                    }
                }
                Accessible.name: i18nc("@label:spinbox", "New preset percentage for %1", chipEditor.accessibleLabel)
            }

            ToolButton {
                // Surfacing the dedupe refusal up front, instead of a click
                // that silently does nothing.
                readonly property bool duplicate: {
                    const fraction = addSpin.value / 100;
                    const list = chipEditor.values || [];
                    for (let i = 0; i < list.length; i++) {
                        if (Math.abs(list[i] - fraction) < chipEditor.dedupeEpsilon)
                            return true;
                    }
                    return false;
                }

                icon.name: "list-add"
                // Kept ENABLED when refusing (full list, duplicate size),
                // greyed instead: a disabled control receives no hover, so
                // the tooltips explaining the refusal could never show (the
                // GroupSortBar precedent). The click no-ops in those states.
                opacity: (chipEditor.full || duplicate) ? 0.5 : 1
                Accessible.name: i18nc("@action:button", "Add preset to %1", chipEditor.accessibleLabel)
                ToolTip.visible: hovered
                ToolTip.text: {
                    if (chipEditor.full)
                        return i18np("This list can hold at most %n preset", "This list can hold at most %n presets", chipEditor.maxCount);
                    if (duplicate)
                        return i18nc("@info:tooltip", "This size is already a preset");
                    return i18nc("@info:tooltip", "Add this size as a preset");
                }
                onClicked: {
                    if (chipEditor.full)
                        return;

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
        text: i18nc("@info:placeholder", "No presets yet. Pick a size and add it.")
    }
}
