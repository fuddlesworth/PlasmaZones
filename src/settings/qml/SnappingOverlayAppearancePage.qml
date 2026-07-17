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
// snappingZonesPage (colour import + border/label bounds). The shader frame rate
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

                    SettingsRow {
                        title: i18n("System accent color")
                        searchAnchor: "systemAccentColor"
                        description: i18n("Use your desktop color scheme for zone colors")

                        SettingsSwitch {
                            id: useSystemColorsSwitch

                            checked: appSettings.useSystemColors
                            accessibleName: i18n("Use system accent color")
                            onToggled: function (newValue) {
                                appSettings.useSystemColors = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Highlight color")
                        searchAnchor: "highlightColor"
                        description: i18n("Color for the active/hovered zone")

                        ColorSwatchRow {
                            color: appSettings.highlightColor
                            onClicked: {
                                highlightColorDialog.selectedColor = appSettings.highlightColor;
                                highlightColorDialog.open();
                            }
                        }
                    }

                    SettingsSeparator {
                        visible: !useSystemColorsSwitch.checked
                    }

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Inactive color")
                        searchAnchor: "inactiveColor"
                        description: i18n("Color for zones that are not hovered")

                        ColorSwatchRow {
                            color: appSettings.inactiveColor
                            onClicked: {
                                inactiveColorDialog.selectedColor = appSettings.inactiveColor;
                                inactiveColorDialog.open();
                            }
                        }
                    }

                    SettingsSeparator {
                        visible: !useSystemColorsSwitch.checked
                    }

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Border color")
                        searchAnchor: "borderColor"
                        description: i18n("Color for zone borders")

                        ColorSwatchRow {
                            color: appSettings.borderColor
                            onClicked: {
                                borderColorDialog.selectedColor = appSettings.borderColor;
                                borderColorDialog.open();
                            }
                        }
                    }

                    SettingsSeparator {
                        visible: !useSystemColorsSwitch.checked
                    }

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Import colors")
                        searchAnchor: "importColors"
                        description: i18n("Load a color scheme from pywal or a JSON file")

                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Button {
                                text: i18n("From pywal")
                                icon.name: "color-management"
                                onClicked: root.zonesBridge.loadColorsFromPywal()
                            }

                            Button {
                                text: i18n("From file…")
                                icon.name: "document-open"
                                onClicked: colorFileDialog.open()
                            }
                        }
                    }

                    Kirigami.InlineMessage {
                        id: colorImportMessage

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        visible: false
                        type: Kirigami.MessageType.Positive
                        Accessible.name: text
                    }

                    Timer {
                        id: colorImportHideTimer

                        interval: 3000
                        onTriggered: colorImportMessage.visible = false
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
                        title: i18n("Border radius")
                        searchAnchor: "borderRadius"
                        description: i18n("Corner rounding of zone borders in pixels")

                        SettingsSpinBox {
                            id: zoneBorderRadiusSpin

                            accessibleName: i18n("Border radius")
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
                headerText: i18n("Zone Labels")
                searchAnchor: "zoneLabels"
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Label color")
                        searchAnchor: "labelColor"
                        description: i18n("Text color for zone labels")

                        ColorSwatchRow {
                            color: appSettings.labelFontColor
                            onClicked: {
                                labelFontColorDialog.selectedColor = appSettings.labelFontColor;
                                labelFontColorDialog.open();
                            }
                        }
                    }

                    SettingsSeparator {
                        visible: !useSystemColorsSwitch.checked
                    }

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
    // COLOR DIALOGS
    // =====================================================================
    ColorDialog {
        id: highlightColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Highlight Color")
        onAccepted: appSettings.highlightColor = selectedColor
    }

    ColorDialog {
        id: inactiveColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Zone Color")
        onAccepted: appSettings.inactiveColor = selectedColor
    }

    ColorDialog {
        id: borderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Border Color")
        onAccepted: appSettings.borderColor = selectedColor
    }

    ColorDialog {
        id: labelFontColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Label Color")
        onAccepted: appSettings.labelFontColor = selectedColor
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

    FileDialog {
        id: colorFileDialog

        title: i18n("Import Colors from File")
        nameFilters: [i18n("JSON files (*.json)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: root.zonesBridge.loadColorsFromFile(settingsController.urlToLocalFile(selectedFile))
    }

    Kirigami.PromptDialog {
        id: colorImportErrorDialog

        title: i18n("Color Import Failed")
        standardButtons: Kirigami.Dialog.Ok
        preferredWidth: Math.min(Kirigami.Units.gridUnit * 30, parent.width * 0.8)
    }

    Connections {
        function onColorImportError(message) {
            colorImportErrorDialog.subtitle = message;
            colorImportErrorDialog.open();
        }

        function onColorImportSuccess() {
            colorImportMessage.text = i18n("Colors imported successfully");
            colorImportMessage.visible = true;
            colorImportHideTimer.restart();
        }

        target: root.zonesBridge
    }
}
