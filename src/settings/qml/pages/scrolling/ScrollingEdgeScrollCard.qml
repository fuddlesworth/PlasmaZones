// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Edge auto-scroll during a drag re-insert.
 *
 * Sits between the Triggers card and the Drop indicator card because it only
 * runs during the drag those triggers arm, and it is what the drop indicator
 * follows while it runs.
 *
 * The three numbers are niri's dnd-edge-view-scroll vocabulary: how wide the
 * band at the working area's edge is, how long the pointer must sit in it
 * before the strip starts moving, and how fast the strip travels once the
 * pointer reaches the very edge. Speed ramps from nothing at the band's inner
 * edge up to the maximum, so where the pointer rests inside the band is the
 * throttle.
 *
 * The engine measures the POINTER against the WORKING AREA, not the dragged
 * window against the screen, so the row descriptions must say so: a drag grab
 * can hold the window far from the pointer, and a panel insets the working
 * area from the screen edge.
 */
SettingsCard {
    id: root

    /// Bounds, read through the controller so C++ stays the single home for
    /// these numbers (the same accessor the other scrolling cards use).
    readonly property var _scrollConsts: settingsController.scrollingConstants()

    headerText: i18n("Edge auto-scroll")
    searchAnchor: "scrollingDragScroll"
    collapsible: true
    // The master switch lives in the card HEADER, like the Drop indicator
    // card's: search deep-links to the rows below land on the
    // invisible-target fallback while the feature is off, and that fallback
    // scrolls to the card header on the assumption its toggle is what
    // un-hides the target.
    showToggle: true
    toggleChecked: appSettings.scrollingDragScrollEnabled
    onToggleClicked: checked => appSettings.scrollingDragScrollEnabled = checked

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Trigger width")
            searchAnchor: "scrollingDragScrollTriggerWidth"
            description: i18n("How close to the edge of the working area the pointer has to be before the strip starts scrolling.")

            SettingsSpinBox {
                id: triggerWidthSpin

                accessibleName: i18n("Edge auto-scroll trigger width")
                from: root._scrollConsts.dragScrollTriggerWidthMin
                to: root._scrollConsts.dragScrollTriggerWidthMax
                stepSize: 5
                onValueModified: value => {
                    appSettings.scrollingDragScrollTriggerWidth = value;
                }
                // Guarded Binding rather than a plain `value:` binding, which
                // SettingsSpinBox's own edit echo destroys after the first
                // edit. Same shape as the drop indicator card's spins.
                Binding on value {
                    value: appSettings.scrollingDragScrollTriggerWidth
                    when: !triggerWidthSpin.editing
                    restoreMode: Binding.RestoreNone
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Start delay")
            searchAnchor: "scrollingDragScrollDelayMs"
            description: i18n("How long the pointer has to stay near the edge before the strip moves. Stops a drag that only passes by an edge from scrolling.")

            SettingsSpinBox {
                id: delaySpin

                accessibleName: i18n("Edge auto-scroll start delay")
                unitText: i18nc("milliseconds unit suffix in a spin box", "ms")
                from: root._scrollConsts.dragScrollDelayMsMin
                to: root._scrollConsts.dragScrollDelayMsMax
                stepSize: 10
                onValueModified: value => {
                    appSettings.scrollingDragScrollDelayMs = value;
                }
                Binding on value {
                    value: appSettings.scrollingDragScrollDelayMs
                    when: !delaySpin.editing
                    restoreMode: Binding.RestoreNone
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Maximum speed")
            searchAnchor: "scrollingDragScrollMaxSpeed"
            description: i18n("How fast the strip scrolls with the pointer held right at the edge. It moves more slowly the further from the edge the pointer sits.")

            SettingsSpinBox {
                id: maxSpeedSpin

                accessibleName: i18n("Edge auto-scroll maximum speed")
                unitText: i18nc("pixels per second unit suffix in a spin box", "px/s")
                from: root._scrollConsts.dragScrollMaxSpeedMin
                to: root._scrollConsts.dragScrollMaxSpeedMax
                stepSize: 50
                onValueModified: value => {
                    appSettings.scrollingDragScrollMaxSpeed = value;
                }
                Binding on value {
                    value: appSettings.scrollingDragScrollMaxSpeed
                    when: !maxSpeedSpin.editing
                    restoreMode: Binding.RestoreNone
                }
            }
        }
    }
}
