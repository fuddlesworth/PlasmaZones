// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Shared "Gaps" page. The inner and outer gaps are a single model used by both
// snapping and tiling, so the controls bind straight to the unified Settings
// gap properties (appSettings.innerGap / outerGap / outerGap*). Smart gaps is
// tiling-only and lives here too because it is a gap behaviour.
SettingsFlickable {
    id: root

    readonly property var bounds: settingsController.gapsPage

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        GapsSettingsCard {
            Layout.fillWidth: true
            searchAnchor: "gaps"
            gapMin: root.bounds.outerGapMin
            gapMax: root.bounds.outerGapMax
            primaryGapMin: root.bounds.innerGapMin
            primaryGapMax: root.bounds.innerGapMax
            primaryGapLabel: i18n("Inner gap")
            primaryGapDescription: i18n("Space between windows")
            outerGapLabel: i18n("Outer gap")
            outerGapDescription: i18n("Space from the screen edges to windows")
            primaryGapValue: appSettings.innerGap
            outerGapValue: appSettings.outerGap
            usePerSideOuterGap: appSettings.usePerSideOuterGap
            outerGapTopValue: appSettings.outerGapTop
            outerGapBottomValue: appSettings.outerGapBottom
            outerGapLeftValue: appSettings.outerGapLeft
            outerGapRightValue: appSettings.outerGapRight
            showSmartGaps: true
            smartGapsValue: appSettings.autotileSmartGaps
            onPrimaryGapModified: value => {
                return appSettings.innerGap = value;
            }
            onOuterGapModified: value => {
                return appSettings.outerGap = value;
            }
            onUsePerSideOuterGapToggled: checked => {
                return appSettings.usePerSideOuterGap = checked;
            }
            onOuterGapTopModified: value => {
                return appSettings.outerGapTop = value;
            }
            onOuterGapBottomModified: value => {
                return appSettings.outerGapBottom = value;
            }
            onOuterGapLeftModified: value => {
                return appSettings.outerGapLeft = value;
            }
            onOuterGapRightModified: value => {
                return appSettings.outerGapRight = value;
            }
            onSmartGapsToggled: checked => {
                return appSettings.autotileSmartGaps = checked;
            }
        }
    }
}
