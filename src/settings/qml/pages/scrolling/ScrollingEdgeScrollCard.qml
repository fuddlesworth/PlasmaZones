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
 * band at the screen edge is, how long the cursor must sit in it before the
 * strip starts moving, and how fast the strip travels once the cursor
 * reaches the very edge. Speed ramps from nothing at the band's inner edge
 * up to the maximum, so where the cursor rests inside the band is the
 * throttle.
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
            description: i18n("How close to the edge of the screen a dragged window has to be before the strip starts scrolling.")
            enabled: appSettings.scrollingDragScrollEnabled

            SettingsSpinBox {
                id: triggerWidthSpin

                accessibleName: i18n("Edge auto-scroll trigger width")
                from: root._scrollConsts.dragScrollTriggerWidthMin
                to: root._scrollConsts.dragScrollTriggerWidthMax
                onValueModified: value => {
                    return appSettings.scrollingDragScrollTriggerWidth = value;
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

        SettingsSeparator {
            enabled: appSettings.scrollingDragScrollEnabled
        }

        SettingsRow {
            title: i18n("Start delay")
            searchAnchor: "scrollingDragScrollDelay"
            description: i18n("How long the window has to stay near the edge before the strip moves, in milliseconds. Stops a drag that only passes by an edge from scrolling.")
            enabled: appSettings.scrollingDragScrollEnabled

            SettingsSpinBox {
                id: delaySpin

                accessibleName: i18n("Edge auto-scroll start delay")
                from: root._scrollConsts.dragScrollDelayMsMin
                to: root._scrollConsts.dragScrollDelayMsMax
                stepSize: 10
                onValueModified: value => {
                    return appSettings.scrollingDragScrollDelayMs = value;
                }
                Binding on value {
                    value: appSettings.scrollingDragScrollDelayMs
                    when: !delaySpin.editing
                    restoreMode: Binding.RestoreNone
                }
            }
        }

        SettingsSeparator {
            enabled: appSettings.scrollingDragScrollEnabled
        }

        SettingsRow {
            title: i18n("Maximum speed")
            searchAnchor: "scrollingDragScrollMaxSpeed"
            description: i18n("How fast the strip scrolls with the window held right at the edge, in pixels per second. It moves more slowly the further from the edge the window sits.")
            enabled: appSettings.scrollingDragScrollEnabled

            SettingsSpinBox {
                id: maxSpeedSpin

                accessibleName: i18n("Edge auto-scroll maximum speed")
                from: root._scrollConsts.dragScrollMaxSpeedMin
                to: root._scrollConsts.dragScrollMaxSpeedMax
                stepSize: 50
                onValueModified: value => {
                    return appSettings.scrollingDragScrollMaxSpeed = value;
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
