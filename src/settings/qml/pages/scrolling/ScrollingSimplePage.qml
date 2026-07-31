// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Simple-mode Scrolling page: the everyday decisions (view centering,
 * default column width) plus the shared Window Handling and Focus cards. The
 * advanced counterpart is ScrollingPage (scrolling-behavior); dirtiness,
 * Reset, and Discard delegate there via simplePageBackingPages.
 *
 * Global scope only, like TilingSimplePage: rows bind appSettings directly
 * and no per-monitor scope chip is offered here — per-monitor overrides are
 * advanced-mode depth. The two shared cards keep their own scope chips; they
 * read the app-wide scope, which is "All monitors" unless the user changed
 * it in advanced mode.
 */
SettingsFlickable {
    id: root

    readonly property var _scrollWidthConsts: settingsController.scrollingWidthConstants()
    readonly property int widthKindProportion: _scrollWidthConsts.kindProportion

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Scrolling")
            searchAnchor: "scrollingSimple"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Center the focused column")
                    searchAnchor: "simpleCenterFocusedColumn"
                    description: i18n("With Never, the strip stays still until the focused column would leave the screen. With Always, the focused column parks in the middle. With On overflow, it centers only once the strip is wider than the screen.")

                    WideComboBox {
                        Accessible.name: i18n("Center the focused column")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Scrolling", "CenterFocusedColumn")
                        storedValue: appSettings.scrollingCenterFocusedColumn
                        onActivated: appSettings.scrollingCenterFocusedColumn = currentValue
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Default width")
                    searchAnchor: "simpleDefaultColumnWidthKind"
                    description: i18n("How wide a column is when it first opens")

                    WideComboBox {
                        Accessible.name: i18n("Default column width")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Scrolling", "DefaultColumnWidthKind")
                        storedValue: appSettings.scrollingDefaultColumnWidthKind
                        onActivated: appSettings.scrollingDefaultColumnWidthKind = currentValue
                    }
                }

                SettingsRow {
                    title: i18n("Proportion of the screen")
                    searchAnchor: "simpleDefaultColumnWidthProportion"
                    description: i18n("How much of the usable screen width a new column takes")
                    enabled: appSettings.scrollingDefaultColumnWidthKind === root.widthKindProportion

                    SettingsSlider {
                        accessibleName: i18n("Proportion of the screen")
                        from: root._scrollWidthConsts.proportionMin
                        to: root._scrollWidthConsts.proportionMax
                        stepSize: root._scrollWidthConsts.proportionStep
                        value: appSettings.scrollingDefaultColumnWidthValue
                        formatValue: function (v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: function (newValue) {
                            appSettings.scrollingDefaultColumnWidthValue = newValue;
                        }
                    }
                }
            }
        }

        ScrollingWindowHandlingCard {
            Layout.fillWidth: true
        }

        ScrollingFocusCard {
            Layout.fillWidth: true
        }
    }
}
