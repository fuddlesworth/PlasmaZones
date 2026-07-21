// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The drag-activation trigger row, shared between the advanced
 * Snapping → Overlay → Behavior page and the simple-mode Snapping page.
 *
 * The row serves dual purpose (#249): with "activate on every drag" on, the
 * same triggers DEACTIVATE the overlay, so the title and description invert.
 * resolveActivationActive in the runtime mirrors that inversion, gated on
 * alwaysActiveOnDrag. Both surfaces host this one component so the inversion
 * is stated once and the runtime has a single wording to stay in step with.
 *
 * Trigger lists are not plain Settings properties, so the hosting page passes
 * its snappingBehaviorPage bridge in. The reveal anchor differs per page and
 * is passed in too, since the search catalog addresses each surface's row by
 * its own id.
 */
SettingsRow {
    id: triggerRow

    /// The snappingBehaviorPage controller (trigger lists live there).
    required property var settingsBridge
    /// Whether the overlay is already on for every drag, which inverts what
    /// holding a trigger does.
    required property bool alwaysActive
    property int sliderPreferredWidth: Kirigami.Units.gridUnit * 16

    title: triggerRow.alwaysActive ? i18n("Hold to deactivate") : i18n("Hold to activate")
    description: triggerRow.alwaysActive ? i18n("Hold a modifier or mouse button while dragging to hide the zone overlay. Esc still cancels the drag entirely.") : i18n("Hold a modifier or mouse button to show zones while dragging")

    ModifierAndMouseCheckBoxes {
        width: triggerRow.sliderPreferredWidth
        allowMultiple: true
        acceptMode: acceptModeAll
        triggers: triggerRow.settingsBridge.dragActivationTriggers
        defaultTriggers: triggerRow.settingsBridge.defaultDragActivationTriggers
        tooltipEnabled: false
        onTriggersModified: triggers => {
            triggerRow.settingsBridge.dragActivationTriggers = triggers;
        }
    }
}
