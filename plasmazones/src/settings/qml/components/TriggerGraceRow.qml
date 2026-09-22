// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief The "release grace period" row shared by every trigger family that
 * has a hold mode.
 *
 * Four families ship one: snapping activation, the snapping zone span
 * modifier, tiling drag re-insert and scrolling drag re-insert. They differ
 * only in wording, in which setting they write, and in the hold-mode
 * condition that gates them, so the row itself lives here rather than being
 * copied per page. Set `enabled` to the host's hold-mode condition and pair
 * the row with a SettingsSeparator carrying the same condition.
 *
 * The host keeps ownership of the value: `graceMs` is read and
 * `graceModified` is written back, so this component stays free of any
 * appSettings reference.
 *
 * Usage:
 *   TriggerGraceRow {
 *       title: i18n("Release grace period")
 *       description: i18n("...")
 *       searchAnchor: "releaseGracePeriod"
 *       accessibleName: i18n("Release grace period for drag activation")
 *       enabled: !alwaysActiveSwitch.checked && !toggleSwitch.checked
 *       minMs: root.settingsBridge.triggerGraceMsMin
 *       maxMs: root.settingsBridge.triggerGraceMsMax
 *       graceMs: appSettings.dragActivationGraceMs
 *       onGraceModified: value => appSettings.dragActivationGraceMs = value
 *   }
 */
SettingsRow {
    id: root

    /// Screen-reader name for the spin box. Qualify it per family — a page can
    /// host two of these rows, and two controls both called "Release grace
    /// period" are indistinguishable to a screen reader.
    required property string accessibleName
    /// Shared bounds, from the page controller's triggerGraceMsMin/Max.
    required property int minMs
    required property int maxMs
    /// Current value, read from the owning setting.
    required property int graceMs

    /// Emitted on spin-box commit only, never per keystroke.
    signal graceModified(int value)

    SettingsSpinBox {
        id: graceSpin

        accessibleName: root.accessibleName
        from: root.minMs
        to: root.maxMs
        stepSize: 10
        // 0..1000 in steps of 10 is a hundred button presses end to end, which
        // is the case SettingsSpinBox's own docs say wants typing. Safe here
        // because the unit lives in a separate label rather than in
        // textFromValue, so the default valueFromText still parses the text.
        editable: true
        unitText: i18nc("milliseconds unit suffix in a spin box", "ms")
        onValueModified: value => root.graceModified(value)

        // Feed the value through a Binding OBJECT rather than a plain
        // `value:` binding: SettingsSpinBox echoes every edit back into its own
        // `value` property, which would destroy a computed binding on the host
        // side after the first edit. Gating on `!editing` keeps a live edit
        // from being overwritten mid-keystroke while still letting a reload,
        // profile switch or per-page Reset refresh the displayed number.
        Binding on value {
            value: root.graceMs
            when: !graceSpin.editing
            restoreMode: Binding.RestoreNone
        }
    }
}
