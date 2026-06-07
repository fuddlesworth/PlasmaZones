// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Bounded container that marks its contents as per-monitor settings.
 *
 * The accent-edged border IS the scope signal: cards placed inside this group
 * are scoped to the monitor chosen in the header DisplayMap; global cards stay
 * outside it. This replaces the old free-floating MonitorSelectorSection that
 * each page wedged in by hand — the component owns placement, so a page can't
 * misplace it.
 *
 * Scope is the app-wide shared `appSettings.scopeScreenName`, so a pick
 * persists across every per-monitor page. The reset button clears the current
 * monitor's overrides for this group's domain.
 *
 * Usage:
 *   ScopedGroup {
 *       appSettings: settingsController
 *       title: i18n("Gaps")
 *       hasOverridesMethod: "hasPerScreenAutotileSettings"
 *       clearerMethod: "clearPerScreenAutotileSettings"
 *
 *       GapsSettingsCard { Layout.fillWidth: true; ... }
 *   }
 */
Item {
    id: root

    required property var appSettings
    // Eyebrow label for the group (e.g. the domain being scoped).
    property string title: ""
    // Q_INVOKABLE names on appSettings for the group's per-screen domain.
    property string hasOverridesMethod: ""
    property string clearerMethod: ""

    // Children land in the content column below the scope header.
    default property alias content: contentColumn.data

    readonly property string scope: appSettings.scopeScreenName
    readonly property bool isPerScreen: scope !== ""
    property bool _hasOverrides: false

    function _refresh() {
        _hasOverrides = isPerScreen && hasOverridesMethod !== "" && appSettings[hasOverridesMethod](scope) === true;
    }
    onScopeChanged: _refresh()
    onHasOverridesMethodChanged: _refresh()
    Component.onCompleted: _refresh()

    Connections {
        target: root.appSettings
        function onPerScreenOverridesChanged() {
            root._refresh();
        }
    }

    Layout.fillWidth: true
    implicitHeight: frame.height
    implicitWidth: frame.implicitWidth

    Rectangle {
        id: frame

        width: root.width
        implicitHeight: outerCol.implicitHeight + Kirigami.Units.largeSpacing * 2
        height: implicitHeight
        radius: Kirigami.Units.smallSpacing * 1.5
        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.04)
        border.width: Math.round(Screen.devicePixelRatio)
        border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5)

        ColumnLayout {
            id: outerCol

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            // Eyebrow + reset.
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: root.title.length > 0 ? root.title : i18nc("@label per-monitor settings group", "Per-monitor")
                    color: Kirigami.Theme.highlightColor
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: i18n("Reset")
                    icon.name: "edit-clear"
                    flat: true
                    visible: root.isPerScreen && root._hasOverrides
                    onClicked: {
                        if (root.clearerMethod !== "")
                            root.appSettings[root.clearerMethod](root.scope);
                    }
                }
            }

            DisplayMap {
                Layout.fillWidth: true
                visible: hasMultiple
                appSettings: root.appSettings
                hasOverridesMethod: root.hasOverridesMethod
            }

            // Scope status line.
            Label {
                Layout.fillWidth: true
                visible: root.isPerScreen
                text: root._hasOverrides ? i18n("Custom settings for this monitor") : i18n("Using default settings (editing below creates an override)")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                height: Math.round(Screen.devicePixelRatio)
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
            }

            // Per-monitor cards land here.
            ColumnLayout {
                id: contentColumn

                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.largeSpacing
            }
        }

        Behavior on border.color {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: Kirigami.Units.shortDuration
            }
        }
    }
}
