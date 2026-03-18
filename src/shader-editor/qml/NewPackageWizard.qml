// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    // ── State exposed to C++ ──
    property string shaderName: ""
    property string shaderCategory: "Custom"
    property string shaderDescription: ""
    property string shaderAuthor: ""
    property int selectedPreset: 0
    property int selectedFeatures: 0

    property bool featureMultipass: false
    property bool featureAudio: false
    property bool featureWallpaper: false
    property bool featuresInitialized: false

    property var presets: typeof initialPresets !== "undefined" ? initialPresets : []
    property int currentStep: 0  // 0=template, 1=details, 2=features

    function computeFeatures() {
        if (!featuresInitialized) {
            selectedFeatures = (presets && presets.length > selectedPreset)
                ? presets[selectedPreset].features : 0
        } else {
            var f = 0
            if (featureMultipass) f |= FeatureMultipass
            if (featureAudio)     f |= FeatureAudio
            if (featureWallpaper) f |= FeatureWallpaper
            selectedFeatures = f
        }
    }

    onSelectedPresetChanged: computeFeatures()
    onFeatureMultipassChanged: computeFeatures()
    onFeatureAudioChanged: computeFeatures()
    onFeatureWallpaperChanged: computeFeatures()

    function goForward() {
        if (currentStep === 0) {
            currentStep = 1
            stack.push(page2Details)
        } else if (currentStep === 1) {
            // Sync feature toggles from the selected preset
            if (presets && presets.length > selectedPreset) {
                // Suppress intermediate recomputes during bulk assignment
                featuresInitialized = false
                var f = presets[selectedPreset].features
                featureMultipass = (f & FeatureMultipass) !== 0
                featureAudio = (f & FeatureAudio) !== 0
                featureWallpaper = (f & FeatureWallpaper) !== 0
                featuresInitialized = true
                computeFeatures()
            }
            currentStep = 2
            stack.push(page3Features)
        }
    }

    function goBack() {
        if (currentStep > 0) {
            // Reset feature init when returning to page 1 so re-selecting
            // a preset will re-sync the toggles on next forward navigation
            if (currentStep === 1) {
                featuresInitialized = false
            }
            currentStep--
            stack.pop()
        }
    }

    // ── Step Indicator + Pages ──
    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: buttonBar.height
        spacing: 0

        // Step indicator bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Kirigami.Theme.alternateBackgroundColor

            RowLayout {
                anchors.centerIn: parent
                spacing: Kirigami.Units.gridUnit * 2

                Repeater {
                    model: [
                        { label: i18n("Template"), step: 0 },
                        { label: i18n("Details"),  step: 1 },
                        { label: i18n("Features"), step: 2 }
                    ]

                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Rectangle {
                            width: 24; height: 24; radius: 12
                            color: root.currentStep >= modelData.step
                                ? Kirigami.Theme.highlightColor
                                : Kirigami.Theme.separatorColor
                            Behavior on color { ColorAnimation { duration: 200 } }

                            Label {
                                anchors.centerIn: parent
                                text: (modelData.step + 1).toString()
                                font.bold: true
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                color: root.currentStep >= modelData.step
                                    ? Kirigami.Theme.highlightedTextColor
                                    : Kirigami.Theme.disabledTextColor
                            }
                        }

                        Label {
                            text: modelData.label
                            font.bold: root.currentStep === modelData.step
                            color: root.currentStep >= modelData.step
                                ? Kirigami.Theme.textColor
                                : Kirigami.Theme.disabledTextColor
                            Behavior on color { ColorAnimation { duration: 200 } }
                        }

                        // Connector line (except after last)
                        Rectangle {
                            visible: modelData.step < 2
                            width: Kirigami.Units.gridUnit; height: 1
                            color: root.currentStep > modelData.step
                                ? Kirigami.Theme.highlightColor
                                : Kirigami.Theme.separatorColor
                            Behavior on color { ColorAnimation { duration: 200 } }
                        }
                    }
                }
            }

            Kirigami.Separator {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
            }
        }

        // Page stack
        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: page1Template

            pushEnter: Transition {
                PropertyAnimation { property: "x"; from: root.width; to: 0; duration: 250; easing.type: Easing.OutCubic }
                PropertyAnimation { property: "opacity"; from: 0.5; to: 1.0; duration: 250 }
            }
            pushExit: Transition {
                PropertyAnimation { property: "x"; from: 0; to: -root.width * 0.3; duration: 250; easing.type: Easing.OutCubic }
                PropertyAnimation { property: "opacity"; from: 1.0; to: 0; duration: 250 }
            }
            popEnter: Transition {
                PropertyAnimation { property: "x"; from: -root.width * 0.3; to: 0; duration: 250; easing.type: Easing.OutCubic }
                PropertyAnimation { property: "opacity"; from: 0; to: 1.0; duration: 250 }
            }
            popExit: Transition {
                PropertyAnimation { property: "x"; from: 0; to: root.width; duration: 250; easing.type: Easing.OutCubic }
                PropertyAnimation { property: "opacity"; from: 1.0; to: 0.5; duration: 250 }
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    // PAGE 1 — Template Gallery
    // ════════════════════════════════════════════════════════════════
    Component {
        id: page1Template

        Item {
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.gridUnit
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Heading {
                    level: 2
                    text: i18n("What would you like to create?")
                    Layout.fillWidth: true
                }

                GridLayout {
                    columns: 3
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Repeater {
                        model: root.presets

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 180
                            Layout.minimumHeight: 130

                            property bool isSelected: root.selectedPreset === index
                            property bool isHovered: cardMouse.containsMouse

                            scale: isSelected ? 1.0 : (isHovered ? 1.02 : 0.97)
                            opacity: isSelected ? 1.0 : (isHovered ? 0.95 : 0.65)
                            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                            Behavior on opacity { NumberAnimation { duration: 150 } }

                            // Selection ring (visible glow behind card)
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -3
                                radius: 11
                                color: "transparent"
                                border.width: parent.isSelected ? 3 : 0
                                border.color: Kirigami.Theme.highlightColor
                                visible: parent.isSelected
                            }

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 2
                                radius: 8
                                border.width: parent.isHovered && !parent.isSelected ? 2 : 0
                                border.color: Kirigami.Theme.highlightColor
                                Behavior on border.width { NumberAnimation { duration: 100 } }

                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: modelData.color1 || "#888" }
                                    GradientStop { position: 1.0; color: modelData.color2 || "#666" }
                                }

                                Kirigami.Icon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: parent.height * 0.12
                                    width: Kirigami.Units.iconSizes.large
                                    height: width
                                    source: modelData.iconName
                                    isMask: true
                                    color: Qt.rgba(1, 1, 1, 0.9)
                                }

                                // Dark scrim — full-card size with same radius, gradient only in lower half
                                Rectangle {
                                    anchors.fill: parent
                                    radius: parent.radius
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: "transparent" }
                                        GradientStop { position: 0.45; color: "transparent" }
                                        GradientStop { position: 0.7; color: Qt.rgba(0, 0, 0, 0.45) }
                                        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.7) }
                                    }
                                }

                                ColumnLayout {
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.margins: Kirigami.Units.smallSpacing * 2.5
                                    spacing: 1
                                    Label {
                                        text: modelData.title; font.bold: true
                                        font.pointSize: Kirigami.Theme.defaultFont.pointSize + 0.5
                                        color: "white"; elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                    Label {
                                        text: modelData.subtitle; color: Qt.rgba(1, 1, 1, 0.75)
                                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                                        wrapMode: Text.WordWrap; Layout.fillWidth: true
                                    }
                                }
                            }

                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.selectedPreset = index
                                onDoubleClicked: {
                                    root.selectedPreset = index
                                    if (root.shaderName.trim().length > 0) wizard.accept()
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    Layout.fillWidth: true
                    Label { text: i18n("Shader Name:"); font.bold: true }
                    TextField {
                        Layout.fillWidth: true
                        placeholderText: i18n("My Custom Shader")
                        text: root.shaderName
                        onTextChanged: root.shaderName = text
                        Keys.onReturnPressed: if (root.shaderName.trim().length > 0) wizard.accept()
                    }
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    // PAGE 2 — Shader Details
    // ════════════════════════════════════════════════════════════════
    Component {
        id: page2Details

        Item {
            RowLayout {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.gridUnit
                spacing: Kirigami.Units.gridUnit

                // ── Left: form fields ──
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 3
                    spacing: Kirigami.Units.largeSpacing

                    Kirigami.Heading {
                        level: 2
                        text: i18n("Shader Details")
                    }

                    Kirigami.FormLayout {
                        Layout.fillWidth: true

                        TextField {
                            Kirigami.FormData.label: i18n("Name:")
                            placeholderText: i18n("My Custom Shader")
                            text: root.shaderName
                            onTextChanged: root.shaderName = text
                            Component.onCompleted: forceActiveFocus()
                        }

                        Label {
                            Kirigami.FormData.label: i18n("Package ID:")
                            text: root.shaderName.trim().length > 0
                                ? (wizard ? wizard.sanitizeId(root.shaderName) : root.shaderName)
                                : i18n("enter-a-name")
                            color: Kirigami.Theme.disabledTextColor
                            font.italic: true
                        }

                        Kirigami.Separator {
                            Kirigami.FormData.isSection: true
                            Kirigami.FormData.label: i18n("Metadata")
                        }

                        ComboBox {
                            Kirigami.FormData.label: i18n("Category:")
                            model: typeof categoryList !== "undefined" ? categoryList : ["Custom"]
                            currentIndex: {
                                var idx = model.indexOf(root.shaderCategory)
                                return idx >= 0 ? idx : 0
                            }
                            onCurrentTextChanged: root.shaderCategory = currentText
                            editable: true
                        }

                        TextField {
                            Kirigami.FormData.label: i18n("Author:")
                            placeholderText: i18n("Your name")
                            text: root.shaderAuthor
                            onTextChanged: root.shaderAuthor = text
                        }

                        TextArea {
                            Kirigami.FormData.label: i18n("Description:")
                            placeholderText: i18n("What does this shader do?")
                            text: root.shaderDescription
                            onTextChanged: root.shaderDescription = text
                            Layout.fillWidth: true
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                            wrapMode: TextEdit.Wrap
                        }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ── Right: selected template summary ──
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 2
                    Layout.fillWidth: true
                    radius: 8
                    color: Kirigami.Theme.alternateBackgroundColor
                    border.width: 1
                    border.color: Kirigami.Theme.separatorColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.largeSpacing

                        Kirigami.Heading { level: 3; text: i18n("Selected Template") }

                        // Mini template card preview
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 100
                            radius: 6
                            clip: true

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: root.presets.length > 0 ? root.presets[root.selectedPreset].color1 : "gray" }
                                GradientStop { position: 1.0; color: root.presets.length > 0 ? root.presets[root.selectedPreset].color2 : "gray" }
                            }

                            Kirigami.Icon {
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: -Kirigami.Units.smallSpacing * 2
                                width: Kirigami.Units.iconSizes.medium
                                height: width
                                source: root.presets.length > 0 ? root.presets[root.selectedPreset].iconName : ""
                                isMask: true
                                color: Qt.rgba(1, 1, 1, 0.9)
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: parent.height * 0.5
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "transparent" }
                                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.65) }
                                }
                            }

                            Label {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.margins: Kirigami.Units.smallSpacing * 2
                                text: root.presets.length > 0 ? root.presets[root.selectedPreset].title : ""
                                font.bold: true
                                color: "white"
                            }
                        }

                        Kirigami.Separator { Layout.fillWidth: true }

                        // Included features summary
                        Kirigami.Heading { level: 4; text: i18n("Includes") }

                        ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Layout.fillWidth: true

                            Label {
                                visible: root.selectedFeatures === 0
                                text: i18n("Base gradient shader with zone masking")
                                color: Kirigami.Theme.disabledTextColor
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: (root.selectedFeatures & FeatureMultipass) !== 0
                                text: "\u2022  " + i18n("Multipass buffer chain")
                                font.pointSize: Kirigami.Theme.smallFont.pointSize + 0.5
                            }
                            Label {
                                visible: (root.selectedFeatures & FeatureAudio) !== 0
                                text: "\u2022  " + i18n("Audio spectrum input")
                                font.pointSize: Kirigami.Theme.smallFont.pointSize + 0.5
                            }
                            Label {
                                visible: (root.selectedFeatures & FeatureWallpaper) !== 0
                                text: "\u2022  " + i18n("Wallpaper sampling")
                                font.pointSize: Kirigami.Theme.smallFont.pointSize + 0.5
                            }
                        }

                        Kirigami.Separator { Layout.fillWidth: true }

                        // Quick file count
                        Kirigami.Heading { level: 4; text: i18n("Generated Files") }
                        Label {
                            property int fileCount: (root.selectedFeatures & FeatureMultipass) ? 4 : 3
                            text: i18n("%1 files (metadata.json, zone.vert, effect.frag%2)",
                                fileCount,
                                (root.selectedFeatures & FeatureMultipass) ? ", pass0.frag" : "")
                            color: Kirigami.Theme.disabledTextColor
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pointSize: Kirigami.Theme.smallFont.pointSize + 0.5
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    // PAGE 3 — Features & Review
    // ════════════════════════════════════════════════════════════════
    Component {
        id: page3Features

        Item {
            RowLayout {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.gridUnit
                spacing: Kirigami.Units.gridUnit

                // ── Left: feature toggles ──
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 3
                    spacing: Kirigami.Units.largeSpacing

                    Kirigami.Heading {
                        level: 2
                        text: i18n("Features & Review")
                    }

                    Label {
                        text: i18n("Toggle features to include in your shader package.")
                        color: Kirigami.Theme.disabledTextColor
                    }

                    Kirigami.Separator { Layout.fillWidth: true }

                    RowLayout {
                        spacing: Kirigami.Units.largeSpacing; Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 1
                            Label { text: i18n("Multipass"); font.bold: true }
                            Label { text: i18n("Two-pass buffer chain for feedback, trails, and simulations. Adds pass0.frag and a Speed parameter."); color: Kirigami.Theme.disabledTextColor; font.pointSize: Kirigami.Theme.smallFont.pointSize; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        Switch { checked: root.featureMultipass; onCheckedChanged: root.featureMultipass = checked }
                    }
                    Kirigami.Separator { Layout.fillWidth: true }

                    RowLayout {
                        spacing: Kirigami.Units.largeSpacing; Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 1
                            Label { text: i18n("Audio-Reactive"); font.bold: true }
                            Label { text: i18n("Spectrum input via audioSpectrum(). Adds Reactivity, Bass Boost, and Color Shift parameters."); color: Kirigami.Theme.disabledTextColor; font.pointSize: Kirigami.Theme.smallFont.pointSize; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        Switch { checked: root.featureAudio; onCheckedChanged: root.featureAudio = checked }
                    }
                    Kirigami.Separator { Layout.fillWidth: true }

                    RowLayout {
                        spacing: Kirigami.Units.largeSpacing; Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 1
                            Label { text: i18n("Wallpaper Sampling"); font.bold: true }
                            Label { text: i18n("Sample the desktop wallpaper via uWallpaper. Adds Blend and Tint parameters."); color: Kirigami.Theme.disabledTextColor; font.pointSize: Kirigami.Theme.smallFont.pointSize; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        Switch { checked: root.featureWallpaper; onCheckedChanged: root.featureWallpaper = checked }
                    }

                    Item { Layout.fillHeight: true }
                }

                // ── Right: manifest panel ──
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 2
                    Layout.fillWidth: true
                    radius: 8
                    color: Kirigami.Theme.alternateBackgroundColor
                    border.width: 1
                    border.color: Kirigami.Theme.separatorColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.largeSpacing

                        Kirigami.Heading { level: 3; text: i18n("Package Contents") }

                        ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing; Layout.fillWidth: true
                            Repeater {
                                model: {
                                    var files = ["metadata.json", "zone.vert", "effect.frag"]
                                    if (root.selectedFeatures & FeatureMultipass) files.push("pass0.frag")
                                    return files
                                }
                                Label {
                                    text: "\u2022  " + modelData
                                    font.family: wizard ? wizard.fixedFontFamily() : "monospace"
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize + 0.5
                                }
                            }
                        }

                        Kirigami.Separator { Layout.fillWidth: true }
                        Kirigami.Heading { level: 3; text: i18n("Parameters") }

                        ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Layout.fillWidth: true; Layout.fillHeight: true
                            Repeater {
                                model: {
                                    var defs = typeof featureParamDefs !== "undefined" ? featureParamDefs : []
                                    var f = root.selectedFeatures
                                    var p = []
                                    for (var i = 0; i < defs.length; i++) {
                                        if (f & defs[i].feature)
                                            p.push(defs[i])
                                    }
                                    if (p.length === 0) p.push({name: "(none)", type: "", range: ""})
                                    return p
                                }
                                Label {
                                    text: modelData.type === ""
                                        ? modelData.name
                                        : (modelData.name + "   " + modelData.type + (modelData.range ? "  " + modelData.range : ""))
                                    font.family: wizard ? wizard.fixedFontFamily() : "monospace"
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize + 0.5
                                    color: modelData.type === "" ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    // BUTTON BAR
    // ════════════════════════════════════════════════════════════════
    Rectangle {
        id: buttonBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 52
        color: Kirigami.Theme.backgroundColor

        Kirigami.Separator {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.gridUnit
            anchors.rightMargin: Kirigami.Units.gridUnit
            spacing: Kirigami.Units.smallSpacing

            Button {
                text: i18n("Cancel")
                onClicked: wizard.reject()
            }

            Item { Layout.fillWidth: true }

            Button {
                text: i18n("Back")
                icon.name: "go-previous"
                visible: root.currentStep > 0
                onClicked: root.goBack()
            }

            Button {
                text: root.currentStep < 2 ? i18n("Next") : ""
                icon.name: "go-next"
                visible: root.currentStep < 2
                onClicked: root.goForward()
            }

            Button {
                text: i18n("Create")
                icon.name: "document-new"
                highlighted: true
                enabled: root.shaderName.trim().length > 0
                onClicked: wizard.accept()
            }
        }
    }
}
