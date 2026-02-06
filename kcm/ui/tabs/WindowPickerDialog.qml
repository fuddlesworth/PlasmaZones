// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dialog for picking from currently running windows
 *
 * Shows a filterable list of running windows with their window class and caption.
 * Used by the Exclusions tab to let users pick window classes without needing
 * to manually type them (replaces the old "use xprop" workflow).
 */
Kirigami.Dialog {
    id: dialog
    title: forApps ? i18n("Pick Application from Running Windows") : i18n("Pick Window Class from Running Windows")
    preferredWidth: Kirigami.Units.gridUnit * 25
    preferredHeight: Kirigami.Units.gridUnit * 20

    required property var kcm

    property bool forApps: false
    property var windowList: []

    // Derive a short app name from windowClass when appName is not available
    // X11: "resourceName resourceClass" → first part (e.g., "dolphin")
    // Wayland app_id: "org.kde.dolphin" → last dot-segment (e.g., "dolphin")
    function deriveAppName(windowClass) {
        if (!windowClass || windowClass.length === 0) return ""
        let spaceIdx = windowClass.indexOf(' ')
        if (spaceIdx > 0) return windowClass.substring(0, spaceIdx)
        let dotIdx = windowClass.lastIndexOf('.')
        if (dotIdx >= 0 && dotIdx < windowClass.length - 1) return windowClass.substring(dotIdx + 1)
        return windowClass
    }

    function openForApps() {
        forApps = true
        refresh()
        open()
        searchField.forceActiveFocus()
    }

    function openForClasses() {
        forApps = false
        refresh()
        open()
        searchField.forceActiveFocus()
    }

    function refresh() {
        searchField.text = ""
        windowList = kcm.getRunningWindows()
    }

    customFooterActions: [
        Kirigami.Action {
            text: i18n("Refresh")
            icon.name: "view-refresh"
            onTriggered: dialog.refresh()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.SearchField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: i18n("Filter...")
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        ListView {
            id: windowListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 14
            clip: true

            model: {
                if (!dialog.windowList || dialog.windowList.length === 0) {
                    return []
                }
                let filter = searchField.text.toLowerCase()
                if (filter.length === 0) {
                    return dialog.windowList
                }
                return dialog.windowList.filter(function(w) {
                    let primary = dialog.forApps
                        ? (w.appName && w.appName.length > 0 ? w.appName : dialog.deriveAppName(w.windowClass))
                        : w.windowClass
                    return primary.toLowerCase().includes(filter) ||
                           w.caption.toLowerCase().includes(filter)
                })
            }

            delegate: ItemDelegate {
                width: ListView.view.width
                highlighted: ListView.isCurrentItem

                readonly property string appNameResolved: modelData.appName && modelData.appName.length > 0
                    ? modelData.appName : dialog.deriveAppName(modelData.windowClass)
                readonly property string primaryText: dialog.forApps ? appNameResolved : modelData.windowClass

                Accessible.name: primaryText + (modelData.caption.length > 0 ? " — " + modelData.caption : "")

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: dialog.forApps ? "application-x-executable" : "window"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true

                        Label {
                            text: primaryText
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Label {
                            text: modelData.caption
                            Layout.fillWidth: true
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            opacity: 0.7
                            elide: Text.ElideRight
                            visible: modelData.caption.length > 0
                        }
                    }
                }

                onClicked: {
                    if (dialog.forApps) {
                        kcm.addExcludedApp(appNameResolved)
                    } else {
                        kcm.addExcludedWindowClass(modelData.windowClass)
                    }
                    dialog.close()
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: windowListView.count === 0
                text: dialog.windowList.length === 0
                    ? i18n("No windows found")
                    : i18n("No matching windows")
                explanation: dialog.windowList.length === 0
                    ? i18n("Make sure the PlasmaZones daemon and KWin effect are running")
                    : i18n("Try a different search term")
            }
        }
    }
}
