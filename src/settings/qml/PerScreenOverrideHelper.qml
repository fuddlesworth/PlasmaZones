// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Reusable per-screen override logic for settings pages
 *
 * Encapsulates the identical per-screen override pattern used across
 * AutotilingTab, ZoneSelectorSection, and ZonesTab. Each tab instantiates
 * this with its feature-specific C++ method names.
 *
 * Usage:
 *   PerScreenOverrideHelper {
 *       id: psHelper
 *       appSettings: root.appSettings
 *       getterMethod: "getPerScreenAutotileSettings"
 *       setterMethod: "setPerScreenAutotileSetting"
 *       clearerMethod: "clearPerScreenAutotileSettings"
 *   }
 *
 * Then: psHelper.settingValue(key, globalValue)
 *       psHelper.writeSetting(key, value, globalSetter)
 */
QtObject {
    required property var appSettings
    required property string getterMethod
    required property string setterMethod
    required property string clearerMethod
    property string selectedScreenName: ""
    readonly property bool isPerScreen: selectedScreenName !== ""
    readonly property bool hasOverrides: isPerScreen && Object.keys(perScreenOverrides).length > 0
    property var perScreenOverrides: ({
    })

    function reload() {
        if (isPerScreen && selectedScreenName !== "")
            perScreenOverrides = appSettings[getterMethod](selectedScreenName);
        else
            perScreenOverrides = {
        };
    }

    function settingValue(key, globalValue) {
        if (isPerScreen && perScreenOverrides.hasOwnProperty(key))
            return perScreenOverrides[key];

        return globalValue;
    }

    function writeSetting(key, value, globalSetter) {
        if (isPerScreen) {
            appSettings[setterMethod](selectedScreenName, key, value);
            var updated = Object.assign({
            }, perScreenOverrides);
            updated[key] = value;
            perScreenOverrides = updated;
        } else {
            globalSetter(value);
        }
    }

    function clearOverrides() {
        appSettings[clearerMethod](selectedScreenName);
        reload();
    }

    onSelectedScreenNameChanged: reload()
}
