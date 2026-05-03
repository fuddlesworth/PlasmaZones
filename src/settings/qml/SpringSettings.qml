// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Spring-physics-specific controls for the Animations card.
 *
 * Mirrors EasingSettings' shape but for spring (omega, zeta) parameters.
 * Mode-agnostic rows (sequence mode, stagger interval, minimum distance)
 * live on the parent page so they remain visible regardless of timing
 * mode. Duration is intentionally absent — spring derives its settle
 * time from the omega/zeta pair.
 *
 * Required properties:
 *   - animationsEnabled: whether animations are currently enabled
 *   - omega:             current spring stiffness/speed (rad/s)
 *   - zeta:              current spring damping ratio
 *
 * Signals:
 *   - springChanged(omega, zeta): emitted when the user adjusts either
 *     parameter. The parent page repacks both into the
 *     `"spring:omega,zeta"` wire format and writes through
 *     `appSettings.animationEasingCurve`, mirroring how
 *     `AnimationsPresetsPage.applyAsDefault()` writes spring presets to
 *     the global path.
 */
ColumnLayout {
    id: springRoot

    required property bool animationsEnabled
    required property real omega
    required property real zeta

    signal springChanged(real omega, real zeta)

    spacing: Kirigami.Units.smallSpacing

    // ── Preset ──────────────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Preset")
        description: i18n("Quick-select spring behavior")

        WideComboBox {
            Accessible.name: i18n("Spring preset")
            enabled: springRoot.animationsEnabled
            displayText: currentIndex < 0 ? i18n("Custom") : currentText
            model: CurvePresets.springPresets.map((p) => {
                return p.label;
            })
            currentIndex: CurvePresets.springPresetIndex(springRoot.omega, springRoot.zeta)
            onActivated: (index) => {
                if (index < 0 || index >= CurvePresets.springPresets.length)
                    return ;

                var p = CurvePresets.springPresets[index];
                springRoot.springChanged(p.omega, p.zeta);
            }
        }

    }

    SettingsSeparator {
    }

    // ── Speed (omega) ───────────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Speed (ω)")
        description: i18n("Higher = faster spring response")

        SettingsSlider {
            enabled: springRoot.animationsEnabled
            from: settingsController.animationsPage.springOmegaMin
            to: settingsController.animationsPage.springOmegaMax
            stepSize: 0.5
            Accessible.name: i18n("Speed")
            value: springRoot.omega
            formatValue: function(v) {
                return v.toFixed(1);
            }
            onMoved: (value) => {
                springRoot.springChanged(value, springRoot.zeta);
            }
        }

    }

    SettingsSeparator {
    }

    // ── Damping ratio (zeta) ────────────────────────────────────────────────
    SettingsRow {
        title: i18n("Damping ratio (ζ)")
        description: i18n("< 1 bouncy, = 1 critical, > 1 overdamped")

        SettingsSlider {
            enabled: springRoot.animationsEnabled
            from: settingsController.animationsPage.springZetaMin
            to: settingsController.animationsPage.springZetaMax
            stepSize: 0.05
            Accessible.name: i18n("Damping ratio")
            value: springRoot.zeta
            formatValue: function(v) {
                return v.toFixed(2);
            }
            onMoved: (value) => {
                springRoot.springChanged(springRoot.omega, value);
            }
        }

    }

}
