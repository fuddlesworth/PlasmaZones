// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations → Presets — built-in and user curve / spring presets.
 *
 * Mirrors PR #291's two-pane layout (Easing / Spring × Built-in / User).
 * Built-ins live in the CurvePresets singleton; user presets are read
 * from `~/.local/share/plasmazones/profiles/` via
 * `AnimationsPageController.userPresets()` (file-per-preset, name field
 * disambiguates from per-event override files).
 *
 * "Use as Default" routes through `appSettings.animationEasingCurve`,
 * which is the existing config-driven Global path. For a spring preset
 * the curve string is the wire form `"spring:omega,zeta"`; the
 * Settings::animationProfile getter routes it through CurveRegistry like
 * any other curve string.
 */
SettingsFlickable {
    id: root

    readonly property var appSettings: settingsController.settings
    // Refresh hook bound to the controller signal below. The list is loaded
    // from a Q_INVOKABLE — QML can't observe the controller's internal state
    // through that boundary, so the Connections block below manually
    // reassigns `userPresetsList` whenever the controller emits a change
    // signal. The cached `_easingUserPresets` / `_springUserPresets`
    // bindings re-evaluate from the new list automatically.
    property var userPresetsList: settingsController.animationsPage.userPresets()
    // QVariantList from C++
    readonly property var _easingUserPresets: filterUserPresets(false)
    // QVariantList from C++
    readonly property var _springUserPresets: filterUserPresets(true)
    property bool _deletingPreset: false

    function isSpringEntry(curveStr) {
        return typeof curveStr === "string" && curveStr.indexOf("spring:") === 0;
    }

    function parseSpring(curveStr) {
        // `|| fallback` coerces falsy values to the fallback — but
        // `0` is falsy in JS, and zeta = 0 is a semantically valid
        // value (undamped oscillator). Test `isFinite` instead so
        // a user-saved `spring:12,0` preset round-trips correctly
        // rather than silently snapping to critically-damped (zeta = 1).
        var parts = curveStr.substring(7).split(",");
        var omega = parseFloat(parts[0]);
        var zeta = parseFloat(parts[1]);
        return {
            "omega": isFinite(omega) ? omega : 12,
            "zeta": isFinite(zeta) ? zeta : 1
        };
    }

    // Filter user presets by easing/spring AND keep the preset name +
    // payload in one entry the QML rows can bind to without further
    // shuffling.
    function filterUserPresets(wantSpring) {
        var result = [];
        for (var i = 0; i < userPresetsList.length; i++) {
            var entry = userPresetsList[i];
            var isSpring = isSpringEntry(entry.curve);
            if (isSpring === wantSpring)
                result.push(entry);
        }
        return result;
    }

    function applyAsDefault(curveStr) {
        // Global path is settings-driven (kSettingsDrivenProfilePaths in
        // src/daemon/daemon.cpp). Writing through the existing
        // animationEasingCurve Q_PROPERTY lets the daemon's
        // publishActiveAnimationProfile pick it up via the same wire
        // every other Global edit uses.
        root.appSettings.animationEasingCurve = curveStr;
    }

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onUserPresetsChanged() {
            root.userPresetsList = settingsController.animationsPage.userPresets();
            root._deletingPreset = false;
        }

        target: settingsController.animationsPage
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ════════════════ EASING PRESETS ════════════════
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Easing Presets")
            searchAnchor: "easingPresets"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Built-ins
                Repeater {
                    model: CurvePresets.quickPresets

                    delegate: RowLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        CurveThumbnail {
                            implicitWidth: Kirigami.Units.gridUnit * 5
                            implicitHeight: Kirigami.Units.gridUnit * 3
                            curve: modelData.curve
                            timingMode: CurvePresets.timingModeEasing
                            Accessible.name: i18n("Curve preview for %1", modelData.label)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData.label
                            color: Kirigami.Theme.textColor
                        }

                        Label {
                            text: modelData.curve
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                        Button {
                            Accessible.name: i18n("Use %1 as default", modelData.label)
                            text: i18n("Use as Default")
                            onClicked: root.applyAsDefault(modelData.curve)
                        }
                    }
                }

                // User presets
                SettingsSeparator {
                    visible: root._easingUserPresets.length > 0
                }

                Repeater {
                    model: root._easingUserPresets

                    delegate: RowLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        CurveThumbnail {
                            implicitWidth: Kirigami.Units.gridUnit * 5
                            implicitHeight: Kirigami.Units.gridUnit * 3
                            curve: modelData.curve || "0.33,1.00,0.68,1.00"
                            timingMode: CurvePresets.timingModeEasing
                            Accessible.name: i18n("Curve preview for %1", modelData.name)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: i18n("★ %1", modelData.name)
                            color: Kirigami.Theme.textColor
                        }

                        Label {
                            text: modelData.curve || ""
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                        Button {
                            Accessible.name: i18n("Use %1 as default", modelData.name)
                            text: i18n("Use as Default")
                            onClicked: root.applyAsDefault(modelData.curve)
                        }

                        Button {
                            Accessible.name: i18n("Delete preset %1", modelData.name)
                            icon.name: "edit-delete"
                            display: AbstractButton.IconOnly
                            ToolTip.text: i18n("Delete preset")
                            ToolTip.visible: hovered
                            enabled: !root._deletingPreset
                            onClicked: easingDeleteConfirm.open()
                        }

                        Kirigami.PromptDialog {
                            id: easingDeleteConfirm

                            title: i18n("Delete preset?")
                            subtitle: i18n("\"%1\" will be permanently removed.", modelData.name)
                            standardButtons: Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
                            onDiscarded: {
                                // Block the button until userPresetsChanged
                                // confirms the removal. removeUserPreset
                                // returns false on rare disk failures (file
                                // race / permissions) and emits no signal in
                                // that case — without restoring the flag
                                // here the button would lock forever.
                                root._deletingPreset = true;
                                if (!settingsController.animationsPage.removeUserPreset(modelData.name))
                                    root._deletingPreset = false;

                                easingDeleteConfirm.close();
                            }
                        }
                    }
                }

                Label {
                    visible: root._easingUserPresets.length === 0
                    text: i18n("No custom easing presets yet. Use \"Save as Preset…\" in the curve editor to create one.")
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    font.italic: true
                }
            }
        }

        // ════════════════ SPRING PRESETS ════════════════
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Spring Presets")
            searchAnchor: "springPresets"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Built-ins
                Repeater {
                    model: CurvePresets.springPresets

                    delegate: RowLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        CurveThumbnail {
                            implicitWidth: Kirigami.Units.gridUnit * 5
                            implicitHeight: Kirigami.Units.gridUnit * 3
                            timingMode: CurvePresets.timingModeSpring
                            curve: ""
                            omega: modelData.omega
                            zeta: modelData.zeta
                            Accessible.name: i18n("Spring preview for %1", modelData.label)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData.label
                            color: Kirigami.Theme.textColor
                        }

                        Label {
                            text: i18n("ω=%1 · ζ=%2", modelData.omega.toFixed(1), modelData.zeta.toFixed(2))
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                        Button {
                            Accessible.name: i18n("Use %1 as default", modelData.label)
                            text: i18n("Use as Default")
                            onClicked: root.applyAsDefault("spring:" + modelData.omega.toFixed(2) + "," + modelData.zeta.toFixed(2))
                        }
                    }
                }

                // User presets
                SettingsSeparator {
                    visible: root._springUserPresets.length > 0
                }

                Repeater {
                    model: root._springUserPresets

                    delegate: RowLayout {
                        id: row

                        required property var modelData
                        readonly property var _spring: root.parseSpring(modelData.curve)

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        CurveThumbnail {
                            implicitWidth: Kirigami.Units.gridUnit * 5
                            implicitHeight: Kirigami.Units.gridUnit * 3
                            timingMode: CurvePresets.timingModeSpring
                            curve: ""
                            omega: row._spring.omega
                            zeta: row._spring.zeta
                            Accessible.name: i18n("Spring preview for %1", modelData.name)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: i18n("★ %1", modelData.name)
                            color: Kirigami.Theme.textColor
                        }

                        Label {
                            text: i18n("ω=%1 · ζ=%2", row._spring.omega.toFixed(1), row._spring.zeta.toFixed(2))
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                        Button {
                            Accessible.name: i18n("Use %1 as default", modelData.name)
                            text: i18n("Use as Default")
                            onClicked: root.applyAsDefault(modelData.curve)
                        }

                        Button {
                            Accessible.name: i18n("Delete preset %1", modelData.name)
                            icon.name: "edit-delete"
                            display: AbstractButton.IconOnly
                            ToolTip.text: i18n("Delete preset")
                            ToolTip.visible: hovered
                            enabled: !root._deletingPreset
                            onClicked: springDeleteConfirm.open()
                        }

                        Kirigami.PromptDialog {
                            id: springDeleteConfirm

                            title: i18n("Delete preset?")
                            subtitle: i18n("\"%1\" will be permanently removed.", modelData.name)
                            standardButtons: Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
                            onDiscarded: {
                                // See easing-preset delete above for the
                                // failure-restoration rationale.
                                root._deletingPreset = true;
                                if (!settingsController.animationsPage.removeUserPreset(modelData.name))
                                    root._deletingPreset = false;

                                springDeleteConfirm.close();
                            }
                        }
                    }
                }

                Label {
                    visible: root._springUserPresets.length === 0
                    text: i18n("No custom spring presets yet. Use \"Save as Preset…\" in the curve editor to create one.")
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    font.italic: true
                }
            }
        }
    }
}
