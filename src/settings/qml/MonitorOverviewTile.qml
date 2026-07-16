// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief One monitor tile in the RulesPage MONITORS strip.
 *
 * Visual style mirrors `DisplayMap`'s per-screen tile — monitor
 * icon (rotated for portrait), display label, "Primary" badge — so the
 * page reads consistently with the rest of the app. A small caption row
 * carries the rule-count / assignment summary unique to this view.
 *
 * Clicking the tile filters the rule list to that monitor; clicking the
 * active tile again clears the filter (the parent decides the semantics).
 */
Rectangle {
    id: tile

    /// Full screen map from `settingsController.screens` (displayLabel,
    /// connectorName, isPrimary, width/height, etc.).
    required property var screenData
    /// Rule-related data: `{ screenId, layoutName, tilingEnabled, ruleCount,
    /// assigned, locked }` from `RuleController.monitorOverview(screens)`,
    /// which emits a tile for every screen. A screen with no pinned rules carries
    /// `assigned: false` and renders a "Not assigned" caption; `locked` is true
    /// when a LockContext rule pins the monitor's layout; the property only
    /// stays `undefined` if no overview payload is supplied at all.
    property var tileData: undefined
    /// True when this tile is the active monitor filter.
    property bool selected: false
    readonly property bool _assigned: tile.tileData !== undefined && tile.tileData.assigned === true
    readonly property bool _isPortrait: (tile.screenData.width || 0) > 0 && (tile.screenData.height || 0) > 0 && tile.screenData.height > tile.screenData.width
    // `|| 0` coerces NaN (Number()-ing a non-numeric QVariant) to zero so a
    // malformed tile payload never lands `NaN` inside `i18np` and produces
    // "NaN rules" in the caption.
    readonly property int _ruleCount: (tile.tileData !== undefined && tile.tileData.ruleCount !== undefined ? Number(tile.tileData.ruleCount) : 0) || 0
    readonly property bool _isPrimary: tile.screenData.isPrimary === true
    /// True when a LockContext rule pins this monitor's layout — drives the lock badge.
    readonly property bool _locked: tile.tileData !== undefined && tile.tileData.locked === true

    signal clicked

    implicitWidth: content.implicitWidth + Kirigami.Units.largeSpacing * 2
    implicitHeight: content.implicitHeight + Kirigami.Units.largeSpacing
    radius: Kirigami.Units.smallSpacing
    color: tile.selected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.1) : tileMouse.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06) : "transparent"
    border.width: 1
    border.color: tile.selected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : tileMouse.activeFocus ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)

    ColumnLayout {
        id: content

        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing / 2

        Kirigami.Icon {
            source: "monitor"
            Layout.preferredWidth: Kirigami.Units.iconSizes.medium
            Layout.preferredHeight: Kirigami.Units.iconSizes.medium
            Layout.alignment: Qt.AlignHCenter
            opacity: tile.selected ? 1 : 0.5
            rotation: tile._isPortrait ? 90 : 0
        }

        Label {
            id: monitorLabel

            text: {
                // Cascade through the available identifiers; a final
                // `Unknown monitor` placeholder guards against the every-
                // field-empty case so the tile (and the Accessible.name
                // interpolation that reads this label) never becomes blank.
                // displayLabel already carries the connector (see
                // screenInfoListToVariantList) — the primary badge below covers
                // the primary indicator.
                return tile.screenData.displayLabel || tile.screenData.name || (tile.tileData ? tile.tileData.screenId : "") || i18n("Unknown monitor");
            }
            font: Kirigami.Theme.smallFont
            Layout.alignment: Qt.AlignHCenter
            opacity: tile.selected ? 1 : 0.5
            elide: Text.ElideRight
            Layout.maximumWidth: Kirigami.Units.gridUnit * 15
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: primaryLabel.implicitWidth + Kirigami.Units.smallSpacing
            height: primaryLabel.implicitHeight + Kirigami.Units.smallSpacing / 2
            radius: height / 2
            color: tile._isPrimary ? Qt.rgba(Kirigami.Theme.positiveTextColor.r, Kirigami.Theme.positiveTextColor.g, Kirigami.Theme.positiveTextColor.b, 0.15) : "transparent"

            Label {
                id: primaryLabel

                anchors.centerIn: parent
                text: i18n("Primary")
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.positiveTextColor
                opacity: tile._isPrimary ? 1 : 0
            }
        }

        // Rule-count / assignment caption — small, secondary line beneath the
        // primary badge so the visual hierarchy still leads with the monitor.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Kirigami.Units.smallSpacing

            // Lock badge — shown when a LockContext rule pins this monitor's
            // layout, mirroring the "object-locked" icon used by the lock-layout
            // rule template and the overlay lock affordances.
            Kirigami.Icon {
                source: "object-locked"
                visible: tile._locked
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                opacity: 0.7
                Accessible.name: i18nc("@info:accessibility monitor tile", "Layout locked")
            }

            Kirigami.Icon {
                source: tile._assigned ? (tile.tileData.tilingEnabled ? "view-grid" : "dialog-cancel") : "edit-none"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                opacity: 0.7
            }

            Label {
                font: Kirigami.Theme.smallFont
                opacity: 0.7
                elide: Text.ElideRight
                Layout.maximumWidth: Kirigami.Units.gridUnit * 13
                text: {
                    // The KI18n placeholder for the implicit count argument is
                    // `%n`, not `%1` — using `%1` here leaves the literal
                    // "%1 rule(s)" in the rendered label.
                    var n = tile._ruleCount;
                    var countLabel = i18np("%n rule", "%n rules", n);
                    if (!tile._assigned)
                        return i18nc("@label monitor caption", "%1 · %2", i18n("Not assigned"), countLabel);

                    // `tilingEnabled` means "the screen's window-management
                    // engine is NOT disabled" for whatever engine it runs
                    // (snapping / autotile / scrolling), so the label stays
                    // engine-agnostic rather than saying "Tiling off".
                    if (!tile.tileData.tilingEnabled)
                        return i18nc("@label monitor caption", "%1 · %2", i18n("Engine off"), countLabel);

                    if (tile.tileData.layoutName && tile.tileData.layoutName.length > 0)
                        return i18nc("@label monitor caption", "%1 · %2", tile.tileData.layoutName, countLabel);

                    return countLabel;
                }
            }
        }
    }

    MouseArea {
        id: tileMouse

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        activeFocusOnTab: true
        // a11y role lives on the focusable item (this MouseArea), so assistive
        // tech sees the role and the focus together. CheckBox role rather than
        // RadioButton — radio buttons imply that exactly one option is selected
        // and that clicking the active item is a no-op, but this tile supports
        // click-to-clear (deselection) so a checkable-button affordance is the
        // correct semantics for screen readers ("checked" / "unchecked" toggles
        // per tile).
        Accessible.role: Accessible.CheckBox
        Accessible.name: i18n("Filter rules to monitor %1", monitorLabel.text)
        Accessible.checked: tile.selected
        Accessible.focusable: true
        Keys.onSpacePressed: tile.clicked()
        Keys.onReturnPressed: tile.clicked()
        // Numpad Enter alias, matching the sibling card components.
        Keys.onEnterPressed: tile.clicked()
        onClicked: tile.clicked()
    }

    Behavior on color {
        PhosphorMotionAnimation {
            profile: "widget.hover"
            durationOverride: Kirigami.Units.shortDuration
        }
    }

    Behavior on border.color {
        PhosphorMotionAnimation {
            profile: "widget.hover"
            durationOverride: Kirigami.Units.shortDuration
        }
    }
}
