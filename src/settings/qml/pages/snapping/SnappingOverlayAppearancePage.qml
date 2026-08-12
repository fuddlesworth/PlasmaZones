// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Snapping → Overlay → Appearance. How the drag-time zone overlay LOOKS: zone
// colours, opacity, borders and labels (the former "Zones" page) merged with
// the overlay effects (numbers, flash — the former "Effects" page). Binds
// snappingZonesPage for border and label bounds. The shader frame rate
// + audio spectrum controls moved to General, since they drive every shader
// category (overlay, animation, surface decoration), not just this overlay.
SettingsFlickable {
    id: root

    readonly property var zonesBridge: settingsController.snappingZonesPage
    readonly property int opacitySliderMax: 100
    // The ISettings object (the `appSettings` context property), captured at
    // page scope. FontPickerDialog declares its own `appSettings:
    // settingsController` (the controller carries the QFontDatabase helper
    // invokables it needs), which SHADOWS the context property inside the
    // dialog's onAccepted — writing `appSettings.labelFontFamily` there hit a
    // nonexistent property on the controller and threw, so font picks never
    // persisted. Write the label font settings through this reference so they
    // always target ISettings (mirrors TilingAlgorithmPage's m-13 capture).
    readonly property var appSettingsObj: appSettings

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // COLORS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: colorsCard.implicitHeight

            SettingsCard {
                id: colorsCard

                anchors.fill: parent
                headerText: i18n("Colors")
                searchAnchor: "colors"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    // Each colour follows the desktop colour scheme until the
                    // user picks one, and resets back per row — the same
                    // theme-fallback vocabulary as the scrolling and Windows
                    // pages, replacing the old all-or-nothing "system accent
                    // color" switch. appSettings.*Raw is the stored string
                    // (EMPTY = follow); the resolved appSettings.* colour
                    // previews what the overlay actually draws.
                    ThemeFallbackColorRow {
                        id: highlightColorRow
                        title: i18n("Highlight color")
                        // Overridden because the title already says "color".
                        swatchAccessibleName: i18nc("@action:button", "Zone highlight color")
                        searchAnchor: "highlightColor"
                        description: i18n("Color for the active/hovered zone. Follows the color scheme unless you pick one. The opacity sliders below replace any transparency carried by the color.")

                        storedColor: appSettings.highlightColorRaw
                        // Alpha-stripped like the tint row's preview: the
                        // fill paints at the opacity slider's alpha, so
                        // previewing the zone alpha here would make the
                        // swatch jump from half-transparent to solid the
                        // moment a colour is picked (picks store opaque).
                        themeColor: Qt.rgba(appSettings.highlightColor.r, appSettings.highlightColor.g, appSettings.highlightColor.b, 1)
                        picker: zoneColorDialog
                        onColorChosen: function (hex) {
                            // The FILL alpha is owned by the opacity sliders
                            // (the paint path premultiplies the fill RGB by
                            // the slider and ignores the colour's own alpha),
                            // so store the pick opaque rather than letting
                            // the shared alpha-capable dialog record a
                            // transparency the zone never draws. The border
                            // and label rows below DO honour alpha, which is
                            // why the dialog keeps the channel.
                            appSettings.highlightColorRaw = hex === highlightColorRow.sentinel ? hex : "#FF" + hex.slice(3);
                        }
                    }

                    SettingsSeparator {}

                    ThemeFallbackColorRow {
                        id: inactiveColorRow
                        title: i18n("Inactive color")
                        // See the highlight row above.
                        swatchAccessibleName: i18nc("@action:button", "Inactive zone color")
                        searchAnchor: "inactiveColor"
                        description: i18n("Color for zones that are not hovered. Follows the color scheme unless you pick one. The opacity sliders below replace any transparency carried by the color.")

                        storedColor: appSettings.inactiveColorRaw
                        // Alpha-stripped — see the highlight row above.
                        themeColor: Qt.rgba(appSettings.inactiveColor.r, appSettings.inactiveColor.g, appSettings.inactiveColor.b, 1)
                        picker: zoneColorDialog
                        onColorChosen: function (hex) {
                            // Fill alpha owned by the opacity sliders — see
                            // the highlight row above.
                            appSettings.inactiveColorRaw = hex === inactiveColorRow.sentinel ? hex : "#FF" + hex.slice(3);
                        }
                    }

                    SettingsSeparator {}

                    ThemeFallbackColorRow {
                        title: i18n("Border color")
                        // See the highlight row above.
                        swatchAccessibleName: i18nc("@action:button", "Zone border color")
                        searchAnchor: "borderColor"
                        description: i18n("Color for zone borders. Follows the color scheme unless you pick one.")

                        storedColor: appSettings.borderColorRaw
                        themeColor: appSettings.borderColor
                        picker: zoneColorDialog
                        onColorChosen: function (hex) {
                            appSettings.borderColorRaw = hex;
                        }
                    }
                }
            }
        }

        // =================================================================
        // OPACITY
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: opacityCard.implicitHeight

            SettingsCard {
                id: opacityCard

                anchors.fill: parent
                headerText: i18n("Opacity")
                searchAnchor: "opacity"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Active opacity")
                        searchAnchor: "activeOpacity"
                        description: i18n("Opacity of the zone under the cursor")

                        SettingsSlider {
                            accessibleName: i18n("Active opacity")
                            from: 0
                            to: root.opacitySliderMax
                            value: appSettings.activeOpacity * root.opacitySliderMax
                            onMoved: value => {
                                return appSettings.activeOpacity = value / root.opacitySliderMax;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Inactive opacity")
                        searchAnchor: "inactiveOpacity"
                        description: i18n("Opacity of zones not under the cursor")

                        SettingsSlider {
                            accessibleName: i18n("Inactive opacity")
                            from: 0
                            to: root.opacitySliderMax
                            value: appSettings.inactiveOpacity * root.opacitySliderMax
                            onMoved: value => {
                                return appSettings.inactiveOpacity = value / root.opacitySliderMax;
                            }
                        }
                    }
                }
            }
        }

        // =================================================================
        // BORDER
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: borderCard.implicitHeight

            SettingsCard {
                id: borderCard

                anchors.fill: parent
                headerText: i18n("Border")
                searchAnchor: "border"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Border width")
                        searchAnchor: "borderWidth"
                        description: i18n("Thickness of zone borders in pixels")

                        SettingsSpinBox {
                            id: zoneBorderWidthSpin

                            accessibleName: i18n("Border width")
                            from: root.zonesBridge.borderWidthMin
                            to: root.zonesBridge.borderWidthMax
                            onValueModified: value => {
                                return appSettings.borderWidth = value;
                            }
                            // Feed value through a guarded Binding so a config change
                            // keeps refreshing the control: a plain `value:` binding is
                            // destroyed by SettingsSpinBox's own edit echo after the
                            // first edit. RestoreNone + the focus gate keeps a live edit
                            // from being clobbered.
                            Binding on value {
                                value: appSettings.borderWidth
                                when: !zoneBorderWidthSpin.editing
                                restoreMode: Binding.RestoreNone
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        // "Corner radius", not "Border radius": the radius
                        // rounds the SHAPE's corners and applies even at zero
                        // border width, so it is not a property of the border.
                        // Matches the window-appearance and tab-indicator rows.
                        title: i18n("Corner radius")
                        searchAnchor: "borderRadius"
                        description: i18n("Corner rounding of zones in pixels")

                        SettingsSpinBox {
                            id: zoneBorderRadiusSpin

                            accessibleName: i18n("Corner radius")
                            from: root.zonesBridge.borderRadiusMin
                            to: root.zonesBridge.borderRadiusMax
                            onValueModified: value => {
                                return appSettings.borderRadius = value;
                            }
                            // See the border width spinbox: guarded Binding so a config
                            // change keeps refreshing after the first edit destroys a
                            // plain binding.
                            Binding on value {
                                value: appSettings.borderRadius
                                when: !zoneBorderRadiusSpin.editing
                                restoreMode: Binding.RestoreNone
                            }
                        }
                    }
                }
            }
        }

        // =================================================================
        // ZONE LABELS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: labelsCard.implicitHeight

            SettingsCard {
                id: labelsCard

                anchors.fill: parent
                headerText: i18n("Zone labels")
                searchAnchor: "zoneLabels"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    ThemeFallbackColorRow {
                        title: i18n("Label color")
                        swatchAccessibleName: i18nc("@action:button", "Zone label text color")
                        searchAnchor: "labelColor"
                        description: i18n("Text color for zone labels. Follows the color scheme unless you pick one.")

                        storedColor: appSettings.labelFontColorRaw
                        themeColor: appSettings.labelFontColor
                        picker: zoneColorDialog
                        onColorChosen: function (hex) {
                            appSettings.labelFontColorRaw = hex;
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Font")
                        searchAnchor: "font"
                        description: i18n("Typeface and style for zone labels")

                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Button {
                                text: appSettings.labelFontFamily || i18n("System default")
                                font.family: appSettings.labelFontFamily
                                font.weight: appSettings.labelFontWeight
                                font.italic: appSettings.labelFontItalic
                                icon.name: "font-select-symbolic"
                                onClicked: {
                                    fontPickerDialog.selectedFamily = appSettings.labelFontFamily;
                                    fontPickerDialog.selectedWeight = appSettings.labelFontWeight;
                                    fontPickerDialog.selectedItalic = appSettings.labelFontItalic;
                                    fontPickerDialog.selectedUnderline = appSettings.labelFontUnderline;
                                    fontPickerDialog.selectedStrikeout = appSettings.labelFontStrikeout;
                                    fontPickerDialog.open();
                                }
                            }

                            Button {
                                icon.name: "edit-clear"
                                visible: appSettings.labelFontFamily !== "" || appSettings.labelFontWeight !== Font.Bold || appSettings.labelFontItalic || appSettings.labelFontUnderline || appSettings.labelFontStrikeout || Math.abs(appSettings.labelFontSizeScale - 1) > 0.01
                                Accessible.name: i18n("Reset to defaults")
                                onClicked: {
                                    appSettings.labelFontFamily = "";
                                    appSettings.labelFontSizeScale = 1;
                                    appSettings.labelFontWeight = Font.Bold;
                                    appSettings.labelFontItalic = false;
                                    appSettings.labelFontUnderline = false;
                                    appSettings.labelFontStrikeout = false;
                                }
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Label scale")
                        searchAnchor: "labelScale"
                        description: i18n("Size multiplier for zone label text")

                        SettingsSlider {
                            accessibleName: i18n("Label scale")
                            from: root.zonesBridge.labelFontScaleMin * 100
                            to: root.zonesBridge.labelFontScaleMax * 100
                            stepSize: 5
                            value: appSettings.labelFontSizeScale * 100
                            onMoved: value => {
                                return appSettings.labelFontSizeScale = value / 100;
                            }
                        }
                    }
                }
            }
        }

        // =================================================================
        // EFFECTS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: effectsCard.implicitHeight

            SettingsCard {
                id: effectsCard

                anchors.fill: parent
                headerText: i18n("Effects")
                searchAnchor: "effects"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Zone numbers")
                        searchAnchor: "zoneNumbers"
                        description: i18n("Display a number label inside each zone")

                        SettingsSwitch {
                            checked: appSettings.showZoneNumbers
                            accessibleName: i18n("Show zone numbers")
                            onToggled: function (newValue) {
                                appSettings.showZoneNumbers = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Flash on layout switch")
                        searchAnchor: "flashOnLayoutSwitch"
                        description: i18n("Briefly flash zones when switching between layouts")

                        SettingsSwitch {
                            checked: appSettings.flashZonesOnSwitch
                            accessibleName: i18n("Flash zones on layout switch")
                            onToggled: function (newValue) {
                                appSettings.flashZonesOnSwitch = newValue;
                            }
                        }
                    }
                }
            }
        }
    }

    // =====================================================================
    // COLOR DIALOG — page-level and shared by the four theme-fallback rows,
    // like the scrolling pages: a page rebuild while a row-scoped dialog is
    // open would tear the popup down under the user. The rows connect
    // transiently and write on accept, so no onAccepted lives here.
    // =====================================================================
    ColorDialog {
        id: zoneColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Zone Color")
    }

    // Publish the open state so page-stepping cannot swap the page out from
    // under an open page-level dialog — same pattern (and standalone-host
    // guard) as RulesPage, covering every dialog this page hosts.
    readonly property bool anyModalOpen: zoneColorDialog.visible || fontPickerDialog.visible
    onAnyModalOpenChanged: {
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = anyModalOpen;
    }
    // Clear a latched true on page swap (RulesPage's own teardown pattern).
    Component.onDestruction: {
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = false;
    }

    FontPickerDialog {
        id: fontPickerDialog

        // The controller on purpose: it carries the QFontDatabase helper
        // invokables (fontStylesForFamily / fontStyleWeight / fontStyleItalic)
        // the dialog calls. It does NOT carry the labelFont* settings, so the
        // writes below go through root.appSettingsObj — NOT the bare
        // `appSettings`, which this declaration shadows in the dialog's scope
        // (see appSettingsObj above).
        appSettings: settingsController
        onAccepted: {
            root.appSettingsObj.labelFontFamily = selectedFamily;
            root.appSettingsObj.labelFontWeight = selectedWeight;
            root.appSettingsObj.labelFontItalic = selectedItalic;
            root.appSettingsObj.labelFontUnderline = selectedUnderline;
            root.appSettingsObj.labelFontStrikeout = selectedStrikeout;
        }
    }
}
