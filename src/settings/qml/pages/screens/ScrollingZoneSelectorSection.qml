// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Strip selector popup settings section (scrolling screens), with
 * per-monitor overrides.
 *
 * The SCROLLING twin of ZoneSelectorSection, composed from the same shared
 * cards. There is no Layout Arrangement card: the strip popup renders one
 * horizontal row of column cards by construction (the strip axis is
 * horizontal), so Position and Preview Size are the whole surface.
 */
ColumnLayout {
    id: root

    required property var appSettings
    required property var controller
    required property QtObject constants
    property real screenAspectRatio: 16 / 9
    readonly property real safeAspectRatio: screenAspectRatio > 0 ? screenAspectRatio : (16 / 9)
    // MIRROR CONTRACT with ZoneSelectorSection: the settingValue /
    // writeSetting forwarders and _writePreviewSize below are identical to
    // the snapping twin's modulo the appSettings property names — change one
    // host, change both. The effective-value LIST is deliberately NOT
    // identical: no LayoutMode / GridColumns / MaxRows here, the strip popup
    // has no arrangement card (see settingscontroller.h). The blocks stay
    // duplicated per host because each binding must read its host's OWN
    // appSettings property in the binding expression for the dependency to
    // be tracked — the psHelper call is fine, the property read is what
    // carries the notify.
    readonly property int effectivePosition: settingValue("Position", appSettings.scrollingZoneSelectorPosition)
    readonly property int effectiveSizeMode: settingValue("SizeMode", appSettings.scrollingZoneSelectorSizeMode)
    readonly property int effectivePreviewWidth: {
        var sm = effectiveSizeMode;
        // Auto mode's ILLUSTRATIVE width, for the miniature preview only.
        // The popup's real Auto width is qBound(120, screenWidth / 10, 280)
        // in computeZoneSelectorLayout, which needs the monitor's pixel
        // width this page does not have — so the preview scales the Medium
        // preset by the aspect ratio instead. Same approximation as the
        // snapping twin; do not read it as the popup's actual size.
        if (sm === 0)
            return Math.round(root.constants.zoneSelectorPreviewMedium * (safeAspectRatio / (16 / 9)));

        return settingValue("PreviewWidth", appSettings.scrollingZoneSelectorPreviewWidth);
    }
    readonly property int effectivePreviewHeight: {
        var sm = effectiveSizeMode;
        if (sm === 0)
            return Math.round(effectivePreviewWidth / safeAspectRatio);

        return settingValue("PreviewHeight", appSettings.scrollingZoneSelectorPreviewHeight);
    }
    readonly property int effectiveTriggerDistance: settingValue("TriggerDistance", appSettings.scrollingZoneSelectorTriggerDistance)

    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    // Same aspect-clamped width+height write as ZoneSelectorSection's.
    function _writePreviewSize(presetWidth) {
        writeSetting("PreviewWidth", presetWidth, function (v) {
            appSettings.scrollingZoneSelectorPreviewWidth = v;
        });
        var newHeight = Math.round(presetWidth / safeAspectRatio);
        newHeight = Math.max(constants.zoneSelectorPreviewHeightMin, Math.min(constants.zoneSelectorPreviewHeightMax, newHeight));
        writeSetting("PreviewHeight", newHeight, function (v) {
            appSettings.scrollingZoneSelectorPreviewHeight = v;
        });
    }

    spacing: Kirigami.Units.largeSpacing

    PerScreenOverrideHelper {
        id: psHelper

        appSettings: root.controller
        selectedScreenName: root.controller.scopeScreenName
        getterMethod: "getPerScreenScrollingZoneSelectorSettings"
        setterMethod: "setPerScreenScrollingZoneSelectorSetting"
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        type: Kirigami.MessageType.Information
        text: i18n("While dragging a window on a scrolling screen, move it to the configured screen edge to open a popup showing the current strip. Drop between two columns to insert a new column, onto a tabbed column to add the window as a tab, or onto the top or bottom half of a column to stack it there.")
        visible: true
    }

    SettingsRow {
        title: i18n("Strip selector popup")
        searchAnchor: "scrollingZoneSelectorEnabled"
        description: i18n("Show the current strip as drop targets when dragging windows to screen edges")

        SettingsSwitch {
            checked: appSettings.scrollingZoneSelectorEnabled
            accessibleName: i18n("Enable strip selector popup")
            onToggled: function (newValue) {
                appSettings.scrollingZoneSelectorEnabled = newValue;
            }
        }
    }

    ZoneSelectorPositionCard {
        controller: root.controller
        constants: root.constants
        featureEnabled: root.appSettings.scrollingZoneSelectorEnabled
        effectivePosition: root.effectivePosition
        effectiveTriggerDistance: root.effectiveTriggerDistance
        scopeHasOverridesMethod: "hasPerScreenScrollingZoneSelectorPositionSettings"
        scopeClearerMethod: "clearPerScreenScrollingZoneSelectorPositionSettings"
        writePosition: function (v) {
            root.writeSetting("Position", v, function (gv) {
                root.appSettings.scrollingZoneSelectorPosition = gv;
            });
        }
        writeTriggerDistance: function (v) {
            root.writeSetting("TriggerDistance", v, function (gv) {
                root.appSettings.scrollingZoneSelectorTriggerDistance = gv;
            });
        }
    }

    ZoneSelectorPreviewSizeCard {
        controller: root.controller
        constants: root.constants
        featureEnabled: root.appSettings.scrollingZoneSelectorEnabled
        safeAspectRatio: root.safeAspectRatio
        effectiveSizeMode: root.effectiveSizeMode
        effectivePreviewWidth: root.effectivePreviewWidth
        effectivePreviewHeight: root.effectivePreviewHeight
        scopeHelper: psHelper
        scopeHasOverridesMethod: "hasPerScreenScrollingZoneSelectorSizeSettings"
        scopeClearerMethod: "clearPerScreenScrollingZoneSelectorSizeSettings"
        writeSizeMode: function (v) {
            root.writeSetting("SizeMode", v, function (gv) {
                root.appSettings.scrollingZoneSelectorSizeMode = gv;
            });
        }
        writePreviewSize: root._writePreviewSize
    }
}
