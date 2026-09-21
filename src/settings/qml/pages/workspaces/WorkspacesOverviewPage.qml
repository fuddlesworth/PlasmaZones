// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dynamic workspaces — Overview leaf.
 *
 * The workspace overview is the zoomed-out, per-monitor view of every
 * workspace (Meta+W by default, or a four-finger swipe up). Its five settings
 * live in Workspaces.Overview and are read by the overview KWin effect over
 * the settings wire, so a change here reaches the open overview on Save
 * without a restart.
 *
 * The backdrop colour is a concrete colour rather than a theme-fallback
 * sentinel: the overview replaces the whole screen, so following the colour
 * scheme would put a light backdrop behind dark workspaces on light schemes.
 * The row still uses the theme-fallback control so a reset puts the default
 * back, with the default itself standing in for the sentinel.
 */
SettingsFlickable {
    id: root

    readonly property string _defaultBackdrop: "#262626"

    ColorDialog {
        id: backdropColorDialog

        title: i18n("Choose Backdrop Color")
        options: ColorDialog.ShowAlphaChannel
    }

    // Publish the open state so page-stepping cannot swap the page out from
    // under the open dialog — same pattern (and standalone-host guard) as
    // ScrollingTabsPage.
    readonly property bool anyModalOpen: backdropColorDialog.visible
    onAnyModalOpenChanged: {
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = anyModalOpen;
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Appearance")
            searchAnchor: "overviewAppearance"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Zoom")
                    searchAnchor: "overviewZoom"
                    description: i18n("How far the overview zooms out when it is fully open. Each workspace is drawn at this fraction of the screen.")

                    SettingsSlider {
                        accessibleName: i18n("Overview zoom")
                        from: 0.1
                        to: 0.75
                        stepSize: 0.05
                        value: appSettings.overviewZoom
                        formatValue: function (v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: function (newValue) {
                            appSettings.overviewZoom = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                ThemeFallbackColorRow {
                    title: i18n("Backdrop")
                    searchAnchor: "overviewBackdropColor"
                    description: i18n("The color drawn behind the zoomed-out workspaces.")
                    storedColor: appSettings.overviewBackdropColor === root._defaultBackdrop ? "" : appSettings.overviewBackdropColor
                    themeColor: root._defaultBackdrop
                    fallbackLabel: i18n("Default")
                    resetAccessibleName: i18nc("@action:button", "Reset the backdrop to the default color")
                    resetToolTip: i18n("Use the default color")
                    picker: backdropColorDialog
                    onColorChosen: function (hex) {
                        appSettings.overviewBackdropColor = hex === "" ? root._defaultBackdrop : hex;
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Workspace names")
                    searchAnchor: "overviewShowWorkspaceNames"
                    description: i18n("Show each workspace's name, or its number, above it.")

                    SettingsSwitch {
                        accessibleName: i18n("Show workspace names")
                        checked: appSettings.overviewShowWorkspaceNames
                        onToggled: function (newValue) {
                            appSettings.overviewShowWorkspaceNames = newValue;
                        }
                    }
                }
            }
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Input")
            searchAnchor: "overviewInput"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Swipe gesture")
                    searchAnchor: "overviewGestureEnabled"
                    description: i18n("Open the overview with a four-finger swipe up on a touchpad, or three fingers on a touch screen.")

                    SettingsSwitch {
                        accessibleName: i18n("Open with a swipe gesture")
                        checked: appSettings.overviewGestureEnabled
                        onToggled: function (newValue) {
                            appSettings.overviewGestureEnabled = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Mouse wheel switches workspaces")
                    searchAnchor: "overviewWheelSwitchesWorkspaces"
                    description: i18n("Scrolling over a monitor's column while the overview is open switches that monitor's workspace.")

                    SettingsSwitch {
                        accessibleName: i18n("Mouse wheel switches workspaces")
                        checked: appSettings.overviewWheelSwitchesWorkspaces
                        onToggled: function (newValue) {
                            appSettings.overviewWheelSwitchesWorkspaces = newValue;
                        }
                    }
                }
            }
        }
    }
}
