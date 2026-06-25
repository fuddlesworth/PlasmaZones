// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Wraps advanced-only settings so they hide as a unit in Simple mode. Bound to
// the persisted advancedMode pref; a ColumnLayout child with visible:false is
// excluded from its parent layout, so the wrapped rows AND their separators take
// no space and leave no dangling dividers.
//
// Usage inside a card's contentItem ColumnLayout — put the section's leading
// SettingsSeparator as the group's FIRST child so the divider hides with it:
//
//     AdvancedGroup {
//         SettingsSeparator {}
//         SettingsRow { ... }   // the advanced setting(s)
//     }
//
// Can also wrap a whole card (an Item in a page's content ColumnLayout) to hide
// an entire section. Search / deep-link reveal auto-enables advancedMode when a
// hit lands inside a group (see SettingsFlickable.revealAnchor).
ColumnLayout {
    readonly property bool isAdvancedGroup: true

    Layout.fillWidth: true
    visible: settingsController.advancedMode
    spacing: Kirigami.Units.smallSpacing
}
