// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations (simple mode) — the pared-down animation surface.
 *
 * Shown ONLY in simple mode (registered SimpleOnly); advanced mode replaces
 * it with the full per-event Animations tree. It exposes the three things a
 * casual user actually wants:
 *   - Enable animations       (the global `animationsEnabled` flag)
 *   - Speed                   (the global `animationDuration`)
 *   - Window open & close     (a single bundled effect applied to the
 *                              window.appearance.open AND .close leaves)
 *
 * The open/close pair legitimately share one effect — a fade/scale reads the
 * same played forward (open) and reversed (close). Minimize and focus are
 * deliberately NOT touched here: they are different motions and keep their
 * own defaults; anyone who wants to assign them individually uses advanced
 * mode. The effect writes ride `AnimationsPageController.setShaderOverride`
 * (the same staging pipeline the per-event cards use), so Save / Discard and
 * the page kebab behave exactly like the rest of the animations tree.
 */
SettingsFlickable {
    id: page

    readonly property QtObject appSettings: settingsController.settings
    readonly property var animController: settingsController.animationsPage

    // The appearance materialise/dissolve pair this page's effect picker
    // drives together. Minimize/focus are intentionally excluded.
    readonly property var _openClosePaths: ["window.appearance.open", "window.appearance.close"]

    // Bundled effects eligible for the open leaf — the picker offers exactly
    // the effects that can actually drive a window-appearance event, filtered
    // by the controller's appliesTo classification. Close accepts the same
    // class, so one list serves both.
    readonly property var _effects: page.animController.availableShaderEffectsForPath("window.appearance.open")

    // The current effect id, read from the open leaf as the representative of
    // the pair. Empty when the leaf carries no override (inheriting the
    // default), which the picker renders as "None".
    readonly property string _currentEffectId: {
        const p = page.animController.resolvedShaderProfile("window.appearance.open");
        return (p && p.effectId) ? p.effectId : "";
    }

    // Combo model: a leading "None" entry (clears the override on both leaves,
    // re-inheriting the default) followed by every eligible bundled effect.
    readonly property var _effectModel: {
        const rows = [
            {
                "text": i18n("None"),
                "value": ""
            }
        ];
        for (let i = 0; i < page._effects.length; ++i)
            rows.push({
                "text": page._effects[i].name,
                "value": page._effects[i].id
            });
        return rows;
    }

    function _applyEffect(effectId) {
        // Write the SAME choice to both leaves so open and close stay a pair.
        // An empty id means "None": clear the override so the leaf re-inherits
        // its default instead of persisting an effect the picker shows as off.
        for (let i = 0; i < page._openClosePaths.length; ++i) {
            const path = page._openClosePaths[i];
            if (effectId.length === 0)
                page.animController.clearShaderOverride(path);
            else
                page.animController.setShaderOverride(path, effectId, {});
        }
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("The essentials. Switch to Advanced in the header for per-event effects, motion, and the shader library.")
        }

        SettingsCard {
            id: animCard

            Layout.fillWidth: true
            headerText: i18n("Animations")
            searchAnchor: "simpleAnimations"
            showToggle: true
            toggleChecked: page.appSettings.animationsEnabled
            onToggleClicked: function (checked) {
                page.appSettings.animationsEnabled = checked;
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Speed")
                    searchAnchor: "animationSpeed"
                    description: i18n("How long each animation takes")

                    SettingsSlider {
                        accessibleName: i18n("Animation speed")
                        from: settingsController.generalPage.animationDurationMin
                        to: settingsController.generalPage.animationDurationMax
                        stepSize: 10
                        value: page.appSettings.animationDuration
                        valueSuffix: " ms"
                        labelWidth: Kirigami.Units.gridUnit * 4
                        onMoved: value => {
                            page.appSettings.animationDuration = Math.round(value);
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Window open & close")
                    searchAnchor: "windowOpenCloseEffect"
                    description: i18n("Effect played when a window appears and disappears")

                    WideComboBox {
                        id: effectCombo

                        Accessible.name: i18n("Window open and close effect")
                        textRole: "text"
                        valueRole: "value"
                        model: page._effectModel
                        onActivated: page._applyEffect(currentValue)

                        // Guarded binding so a user activation can't sever it and
                        // an external profile change keeps the control in sync;
                        // the popup gate leaves an open dropdown alone mid-pick.
                        Binding on currentIndex {
                            value: Math.max(0, effectCombo.indexOfValue(page._currentEffectId))
                            when: !effectCombo.popup.visible
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }
            }
        }
    }
}
