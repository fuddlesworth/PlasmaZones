// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../../js/PresetList.js" as PresetList

/**
 * @brief Editor for one scrolling preset list (column widths or window
 * heights), following the Virtual Screens page's preset-card convention: a
 * uniform card grid where each preset draws its share as a highlight band
 * inside a screen-shaped thumbnail well (horizontal share for widths,
 * vertical for heights), with a per-card remove button and an add row
 * beneath the grid. Cards read left to right, top to bottom in cycle
 * order; new entries append.
 *
 * The stored value stays the canonical comma-joined fraction string. Every
 * edit joins the working array and writes it through `commit`, then the
 * `presets` binding delivers back whatever the schema's canonicalizer kept
 * (dropped duplicates, the 16-entry cap, the nothing-survives default), so
 * the grid always shows the effective presets. The editor refuses the adds
 * the canonicalizer would silently swallow instead of letting the button
 * look like it did nothing.
 *
 * Accessible names arrive as whole sentences from the call site rather than
 * being composed from a noun fragment here, so a translator sees each full
 * string and can order its parts freely.
 */
ColumnLayout {
    id: editor

    /// Canonical comma-joined fraction string (bind an appSettings property).
    required property string presets
    /// Called with the new comma-joined string on every edit.
    required property var commit
    /// Accessible name for one preset card. Receives the percent as %1.
    required property string cardName
    /// Accessible name for a card's remove button. Receives the percent as %1.
    required property string removeName
    /// Accessible name for the add row's percentage field.
    required property string addValueName
    /// Accessible name for the add button.
    required property string addName
    /// Heights preview as a vertical share of the well; widths (the
    /// default) as a horizontal one.
    property bool vertical: false

    readonly property var _values: PresetList.values(presets)
    /// The canonicalizer's entry cap, derived from the schema's stored-index
    /// ceiling so the button gate cannot drift from the store's.
    readonly property int _maxEntries: settingsController.scrollingConstants().presetIndexMax + 1
    /// The percentages already on screen. An add that rounds onto one of
    /// these would render a second, indistinguishable card.
    readonly property var _shownPercents: {
        var out = [];
        for (var i = 0; i < editor._values.length; i++)
            out.push(PresetList.percent(editor._values[i]));
        return out;
    }
    readonly property bool _atCap: editor._values.length >= editor._maxEntries
    readonly property bool _wouldCollide: editor._shownPercents.indexOf(addSpin.value) >= 0

    spacing: Kirigami.Units.smallSpacing

    function _commitList(list) {
        commit(list.join(","));
    }

    /// The first percentage on the spin's own step grid that no card already
    /// shows, searched outward from the midpoint so the seed stays somewhere
    /// sensible rather than at an edge. Falls back to the midpoint when the
    /// list somehow occupies every step, which the entry cap makes
    /// unreachable but which must still answer a number.
    ///
    /// The downward arm stops at the spin's own floor. Walking to 50 - 50
    /// would answer 0, which is below `from` and so a value the control
    /// cannot hold; the spin would silently clamp it and the seed would no
    /// longer be the free percentage this function promised.
    function _firstFreePercent() {
        for (var delta = 0; delta <= 50; delta += 5) {
            if (editor._shownPercents.indexOf(50 + delta) < 0)
                return 50 + delta;
            if (delta > 0 && 50 - delta >= addSpin.from && editor._shownPercents.indexOf(50 - delta) < 0)
                return 50 - delta;
        }
        return 50;
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        type: Kirigami.MessageType.Information
        // Exactly one, not one-or-fewer: at zero entries there is no card on
        // screen for the floor message to be about, and it claimed a preset
        // could not be removed while none was shown.
        visible: editor._atCap || editor._wouldCollide || editor._values.length === 1
        text: {
            if (editor._atCap)
                return i18n("This list is full at %1 presets. Remove one to add another.", editor._maxEntries);
            if (editor._wouldCollide)
                return i18n("There is already a preset at this percentage.");
            // The floor the Remove buttons are greyed at. Explained here
            // because a disabled button receives no hover and so can carry no
            // tooltip of its own.
            return i18n("A list needs at least one preset, so this one cannot be removed.");
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 4
        uniformCellWidths: true
        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        Repeater {
            model: editor._values

            delegate: Rectangle {
                id: presetCard

                required property string modelData
                required property int index

                readonly property real fraction: PresetList.fraction(presetCard.modelData)
                readonly property int percent: PresetList.percent(presetCard.modelData)
                readonly property real _cardPad: Kirigami.Units.largeSpacing

                Layout.fillWidth: true
                implicitHeight: cardRow.implicitHeight + presetCard._cardPad * 2
                radius: Kirigami.Units.smallSpacing * 1.5
                color: Kirigami.Theme.backgroundColor
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                // AT-SPI clients skip items reporting NoRole, which would make
                // the per-card percent unreadable, so the card claims a role.
                Accessible.role: Accessible.ListItem
                Accessible.name: editor.cardName.arg(presetCard.percent)

                RowLayout {
                    id: cardRow

                    anchors.fill: parent
                    anchors.margins: presetCard._cardPad
                    spacing: Kirigami.Units.largeSpacing

                    // Thumbnail: an inset screen-shaped well with the preset's
                    // share drawn as an accent band — the same visual grammar
                    // as the Virtual Screens preset thumbnails.
                    Rectangle {
                        id: thumbnail

                        readonly property real innerPad: Kirigami.Units.smallSpacing

                        Layout.preferredHeight: Kirigami.Units.gridUnit * 2.5
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 2.5 * 16 / 9
                        Layout.alignment: Qt.AlignVCenter
                        radius: Kirigami.Units.smallSpacing
                        color: Kirigami.Theme.alternateBackgroundColor
                        border.width: 1
                        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

                        Rectangle {
                            anchors.centerIn: parent
                            width: editor.vertical ? parent.width - thumbnail.innerPad * 2 : Math.max(2, (parent.width - thumbnail.innerPad * 2) * presetCard.fraction)
                            height: editor.vertical ? Math.max(2, (parent.height - thumbnail.innerPad * 2) * presetCard.fraction) : parent.height - thumbnail.innerPad * 2
                            radius: Kirigami.Units.smallSpacing / 2
                            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.25)
                            border.width: 1
                            border.color: Kirigami.Theme.highlightColor
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: i18nc("a preset size as a percentage of the work area", "%1%", presetCard.percent)
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    ToolButton {
                        Layout.alignment: Qt.AlignTop
                        icon.name: "edit-delete-remove"
                        Accessible.name: editor.removeName.arg(presetCard.percent)
                        display: AbstractButton.IconOnly
                        // Removing the last entry commits an empty string and
                        // the canonicalizer answers with the factory list, so
                        // three cards would reappear unexplained. Floor the
                        // button at one and leave the canonicalizer as the
                        // backstop for values that reach the store elsewhere.
                        enabled: editor._values.length > 1
                        onClicked: {
                            var next = editor._values.slice();
                            next.splice(presetCard.index, 1);
                            editor._commitList(next);
                        }
                        // Only the enabled text: a disabled QQC2 control gets
                        // no hover events, so the disabled variant could
                        // never be shown. The floor is explained by the
                        // InlineMessage above instead, which is visible
                        // exactly when the button is greyed.
                        ToolTip.text: i18n("Remove this preset")
                        ToolTip.visible: hovered
                    }
                }
            }
        }

        // The grid materialises only as many columns as it has items, so a
        // short list would stretch two or three cards across the full width
        // and break the uniform card size. Spacers hold the missing cells.
        Repeater {
            model: (4 - editor._values.length % 4) % 4

            delegate: Item {
                Layout.fillWidth: true
            }
        }
    }

    RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            text: i18n("Add preset:")
        }

        SettingsSpinBox {
            id: addSpin

            accessibleName: editor.addValueName
            unitText: i18nc("percent unit suffix in a spin box", "%")
            from: 1
            to: 100
            stepSize: 5
            // An imperative seed, not a `value:` binding: SettingsSpinBox
            // writes its own `value` back on every edit, which would sever a
            // binding here on the first keystroke.
            //
            // Seeded to the first percentage the list does not already hold,
            // walking the step grid from the midpoint. A flat 50 collided
            // with the shipped 0.5 default in BOTH lists, so the card opened
            // with a collision warning showing and Add greyed out before the
            // user had touched anything.
            Component.onCompleted: addSpin.value = editor._firstFreePercent()
        }

        Button {
            text: i18n("Add")
            icon.name: "list-add"
            flat: true
            Accessible.name: editor.addName
            // Both refusals the canonicalizer would otherwise make silently:
            // the entry cap, and a value that rounds onto a percentage already
            // on a card (0.330 beside the default 0.333 would draw two cards
            // both reading "33%").
            enabled: !editor._atCap && !editor._wouldCollide
            onClicked: {
                var next = editor._values.slice();
                // Three decimals keeps 1/3-style entries distinct without
                // fighting the canonicalizer's number formatting.
                next.push((addSpin.value / 100).toFixed(3));
                editor._commitList(next);
                // Step the spin off the percentage just added. The new card
                // carries that percentage, so leaving the box on it brings
                // the collision warning and a greyed Add straight back — the
                // same dead end the initial seed exists to avoid, one
                // interaction later, and on every add after it.
                //
                // Deferred because `commit` round-trips through the store's
                // canonicalizer, so `_shownPercents` has not settled inside
                // this handler yet. Driven from the click and not from
                // `presets` changing, because that would also fire on Remove
                // and on writes from elsewhere, moving the box under a user
                // who has typed a value and not yet added it.
                Qt.callLater(function () {
                    addSpin.value = editor._firstFreePercent();
                });
            }
        }
    }
}
