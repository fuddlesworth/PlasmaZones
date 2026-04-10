// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // QVariantList of QVariantMap from C++ Q_INVOKABLE — no typed QML equivalent
    property var shaderList: settingsController.availableAnimationShaders()

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onAnimationProfileTreeChanged() {
            root.shaderList = settingsController.availableAnimationShaders();
        }

        target: appSettings
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═════════════════════════════════════════════════════════
        // SHADER MESH SETTINGS
        // ═════════════════════════════════════════════════════════
        SettingsCard {
            id: meshCard

            Layout.fillWidth: true
            headerText: i18n("Shader Mesh")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Subdivisions")
                    description: i18n("Polygon grid density for vertex shader deformation")

                    SettingsSlider {
                        Accessible.name: i18n("Animation shader subdivisions")
                        from: 1
                        to: 64
                        stepSize: 1
                        value: appSettings.animationShaderSubdivisions
                        labelWidth: Kirigami.Units.gridUnit * 5
                        formatValue: function(v) {
                            var n = Math.round(v);
                            return n === 1 ? i18n("1 (off)") : i18n("%1\u00D7%1", n);
                        }
                        onMoved: (value) => {
                            appSettings.animationShaderSubdivisions = Math.round(value);
                        }
                    }

                }

            }

        }

        // ═════════════════════════════════════════════════════════
        // INSTALLED ANIMATIONS
        // ═════════════════════════════════════════════════════════
        SettingsCard {
            id: installedCard

            Layout.fillWidth: true
            headerText: i18n("Installed Animations")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    visible: root.shaderList.length === 0
                    Layout.fillWidth: true
                    text: i18n("No animations available.")
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                }

                Repeater {
                    model: root.shaderList

                    delegate: ColumnLayout {
                        id: shaderDelegate

                        required property var modelData
                        required property int index
                        property bool detailsExpanded: false

                        Layout.fillWidth: true
                        spacing: 0

                        SettingsSeparator {
                            visible: shaderDelegate.index > 0
                        }

                        SettingsRow {
                            title: shaderDelegate.modelData.name ?? ""
                            description: shaderDelegate.modelData.description ?? ""

                            RowLayout {
                                spacing: Kirigami.Units.smallSpacing

                                Label {
                                    visible: shaderDelegate.modelData.hasVertexShader ?? false
                                    text: i18n("Vertex")
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.positiveTextColor
                                }

                                Label {
                                    visible: (shaderDelegate.modelData.subdivisions ?? 1) > 1
                                    text: i18n("%1\u00D7 mesh", shaderDelegate.modelData.subdivisions)
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                }

                                Label {
                                    visible: (shaderDelegate.modelData.parameters ?? []).length > 0
                                    text: i18n("%1 param(s)", (shaderDelegate.modelData.parameters ?? []).length)
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                }

                                Label {
                                    text: shaderDelegate.modelData.isUserShader ? i18n("User") : i18n("System")
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                }

                                ToolButton {
                                    visible: (shaderDelegate.modelData.parameters ?? []).length > 0
                                    icon.name: shaderDelegate.detailsExpanded ? "collapse-all" : "expand-all"
                                    icon.width: Kirigami.Units.iconSizes.small
                                    icon.height: Kirigami.Units.iconSizes.small
                                    display: ToolButton.IconOnly
                                    Accessible.name: i18n("Toggle parameter details for %1", shaderDelegate.modelData.name ?? "")
                                    ToolTip.text: shaderDelegate.detailsExpanded ? i18n("Hide parameters") : i18n("Show parameters")
                                    ToolTip.visible: hovered
                                    ToolTip.delay: Kirigami.Units.toolTipDelay
                                    onClicked: shaderDelegate.detailsExpanded = !shaderDelegate.detailsExpanded
                                }

                            }

                        }

                        // ── Parameter detail listing (read-only reference) ──
                        ColumnLayout {
                            visible: shaderDelegate.detailsExpanded
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.largeSpacing * 2
                            Layout.bottomMargin: Kirigami.Units.smallSpacing
                            spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                            Repeater {
                                model: shaderDelegate.modelData.parameters ?? []

                                delegate: RowLayout {
                                    required property var modelData

                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing

                                    Label {
                                        text: modelData.name || modelData.id || ""
                                        font: Kirigami.Theme.smallFont
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        text: {
                                            var t = modelData.type || "";
                                            if (t === "float" || t === "int") {
                                                var lo = modelData.min !== undefined ? modelData.min : "?";
                                                var hi = modelData.max !== undefined ? modelData.max : "?";
                                                return i18n("%1 [%2 .. %3]", t, lo, hi);
                                            }
                                            return t;
                                        }
                                        font: Kirigami.Theme.smallFont
                                        color: Kirigami.Theme.disabledTextColor
                                        Layout.fillWidth: true
                                    }

                                }

                            }

                        }

                    }

                }

                SettingsSeparator {
                    visible: root.shaderList.length > 0
                }

                SettingsRow {
                    title: i18n("Custom shaders")
                    description: i18n("Install animation shaders to the user directory")

                    Button {
                        text: i18n("Open Directory")
                        icon.name: "document-open-folder"
                        onClicked: settingsController.openAnimationShaderDirectory()
                        Accessible.name: i18n("Open user animation shader directory")
                    }

                }

            }

        }

    }

}
