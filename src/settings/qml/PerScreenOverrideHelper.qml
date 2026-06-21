// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Reusable per-screen override logic for settings pages
 *
 * Encapsulates the identical per-screen override pattern used across the
 * per-monitor settings pages (Tiling Algorithm, Tiling Appearance, Snapping
 * Window Appearance, and ZoneSelectorSection). Each instantiates this with
 * its feature-specific C++ method names.
 *
 * Usage:
 *   PerScreenOverrideHelper {
 *       id: psHelper
 *       appSettings: root.appSettings
 *       getterMethod: "getPerScreenAutotileSettings"
 *       setterMethod: "setPerScreenAutotileSetting"
 *   }
 *
 * Then: psHelper.settingValue(key, globalValue)
 *       psHelper.writeSetting(key, value, globalSetter)
 *
 * Resetting a monitor's overrides is owned by the card's MonitorScopeChip
 * (its scopeClearerMethod), not this helper — the helper reloads reactively
 * via the appSettings.perScreenOverridesChanged signal below.
 */
QtObject {
    id: helper

    required property var appSettings
    required property string getterMethod
    required property string setterMethod
    property string selectedScreenName: ""
    readonly property bool isPerScreen: selectedScreenName !== ""
    readonly property bool hasOverrides: isPerScreen && Object.keys(perScreenOverrides).length > 0
    property var perScreenOverrides: ({})

    function reload() {
        // Gate on selectedScreenName directly, NOT the derived isPerScreen
        // property. reload() runs from onSelectedScreenNameChanged, and on the
        // ""→screen transition the isPerScreen binding has not necessarily
        // recomputed yet when this handler fires. Reading a stale isPerScreen
        // (false) would take the else branch and wipe the freshly-scoped
        // overrides to {}, leaving the control stuck on the global value when
        // switching from "All Monitors" to a specific monitor (discussion #661).
        if (selectedScreenName !== "")
            perScreenOverrides = appSettings[getterMethod](selectedScreenName);
        else
            perScreenOverrides = {};
    }

    function settingValue(key, globalValue) {
        if (isPerScreen && perScreenOverrides.hasOwnProperty(key))
            return perScreenOverrides[key];

        return globalValue;
    }

    function writeSetting(key, value, globalSetter) {
        if (isPerScreen) {
            // The setter emits perScreenOverridesChanged synchronously, so
            // _overrideWatch.reload() reinstalls the authoritative (validated,
            // possibly clamped) override map before this returns. Don't write an
            // optimistic local copy — it would clobber that with the raw input
            // and diverge from the backend on any clamped/rejected value.
            appSettings[setterMethod](selectedScreenName, key, value);
        } else {
            globalSetter(value);
        }
    }

    onSelectedScreenNameChanged: reload()

    // Reload when overrides change anywhere for this domain (e.g. a
    // MonitorScopeChip "Reset this monitor" clears the current monitor) so
    // bound card values refresh even though the selected screen didn't change.
    property Connections _overrideWatch: Connections {
        target: helper.appSettings
        function onPerScreenOverridesChanged() {
            helper.reload();
        }
    }
}
