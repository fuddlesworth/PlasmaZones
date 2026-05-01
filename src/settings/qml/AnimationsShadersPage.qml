// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations → Shaders — installed shader effect browser.
 *
 * Read-only listing. Per-event shader assignment lives in each event
 * card (AnimationEventCard's shader picker section); this page exists
 * so users can survey what's installed, see parameter metadata, and
 * jump to the user shader directory to drop in their own packs.
 */
Flickable {
    id: root

    // Loaded from a Q_INVOKABLE; the Connections block below manually
    // refreshes it on shaderEffectsChanged. See AnimationEventCard.qml's
    // shaderCombo for the same pattern (Q_INVOKABLE results aren't
    // reactive across the QML binding boundary).
    property var effectList: settingsController.animationsPage.availableShaderEffects()
    // Cached at component creation so the binding doesn't re-invoke
    // userShaderDirectoryPath() on every paint. Pure path accessor (no
    // mkpath side effect; see ensureUserShaderDirectory() / the Open
    // Directory button below for the create-if-missing path). The
    // directory is stable for the process lifetime.
    readonly property string _userShaderDir: settingsController.animationsPage.userShaderDirectoryPath()

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onShaderEffectsChanged() {
            root.effectList = settingsController.animationsPage.availableShaderEffects();
        }

        target: settingsController.animationsPage
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: true
            text: i18n("Browse installed animation shaders. Assign shaders to specific events using the shader picker on any per-event sub-page (Window, Zone, OSD, etc.).")
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("User shaders")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18n("User-installed shader packs live under your data directory. Drop a shader pack folder here to make it available to PlasmaZones.")
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                }

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: root._userShaderDir
                        elide: Text.ElideMiddle
                        font: Kirigami.Theme.smallFont
                    }

                    Button {
                        text: i18n("Open Directory")
                        icon.name: "folder-open"
                        Accessible.name: i18n("Open user shader directory")
                        onClicked: settingsController.animationsPage.openUserShaderDirectory()
                    }

                }

            }

        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Installed shaders (%1)", root.effectList.length)

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    visible: root.effectList.length === 0
                    text: i18n("No shader effects installed.")
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                    Layout.fillWidth: true
                }

                Repeater {
                    model: root.effectList

                    delegate: Item {
                        id: shaderRow

                        required property var modelData
                        property bool expanded: false

                        Layout.fillWidth: true
                        implicitHeight: rowColumn.implicitHeight

                        ColumnLayout {
                            id: rowColumn

                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: Kirigami.Units.smallSpacing

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        Label {
                                            text: shaderRow.modelData.name || shaderRow.modelData.id
                                            font.weight: Font.DemiBold
                                        }

                                        Label {
                                            visible: shaderRow.modelData.isUserEffect
                                            text: i18n("User")
                                            font: Kirigami.Theme.smallFont
                                            color: Kirigami.Theme.positiveTextColor
                                        }

                                        Label {
                                            visible: shaderRow.modelData.category && shaderRow.modelData.category.length > 0
                                            text: shaderRow.modelData.category
                                            font: Kirigami.Theme.smallFont
                                            color: Kirigami.Theme.disabledTextColor
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: i18np("%1 parameter", "%1 parameters", (shaderRow.modelData.parameters || []).length)
                                            font: Kirigami.Theme.smallFont
                                            color: Kirigami.Theme.disabledTextColor
                                        }

                                    }

                                    Label {
                                        visible: shaderRow.modelData.description && shaderRow.modelData.description.length > 0
                                        Layout.fillWidth: true
                                        text: shaderRow.modelData.description
                                        wrapMode: Text.WordWrap
                                        color: Kirigami.Theme.disabledTextColor
                                        font: Kirigami.Theme.smallFont
                                    }

                                }

                                ToolButton {
                                    visible: (shaderRow.modelData.parameters || []).length > 0
                                    icon.name: shaderRow.expanded ? "go-up" : "go-down"
                                    Accessible.name: shaderRow.expanded ? i18n("Hide parameters") : i18n("Show parameters")
                                    onClicked: shaderRow.expanded = !shaderRow.expanded
                                }

                            }

                            // Expanded parameter list (read-only metadata)
                            ColumnLayout {
                                visible: shaderRow.expanded
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.gridUnit
                                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                                Repeater {
                                    model: shaderRow.modelData.parameters || []

                                    delegate: RowLayout {
                                        required property var modelData

                                        spacing: Kirigami.Units.smallSpacing

                                        Label {
                                            text: modelData.name || modelData.id
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            text: modelData.type
                                            color: Kirigami.Theme.disabledTextColor
                                            font: Kirigami.Theme.smallFont
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                                        }

                                        Label {
                                            text: {
                                                if (modelData.minValue !== undefined && modelData.maxValue !== undefined) {
                                                    // Format floats to 2 decimals so a default like
                                                    // 0.123456789 doesn't render with 9 digits in
                                                    // the per-shader range display.
                                                    const fmt = (v) => {
                                                        return Number.isInteger(v) ? String(v) : Number(v).toFixed(2);
                                                    };
                                                    return i18n("[%1 .. %2]", fmt(modelData.minValue), fmt(modelData.maxValue));
                                                }
                                                return "";
                                            }
                                            color: Kirigami.Theme.disabledTextColor
                                            font: Kirigami.Theme.smallFont
                                            Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                        }

                                        Label {
                                            text: i18n("default: %1", modelData.defaultValue !== undefined ? modelData.defaultValue : "")
                                            color: Kirigami.Theme.disabledTextColor
                                            font: Kirigami.Theme.smallFont
                                            Layout.fillWidth: true
                                        }

                                    }

                                }

                            }

                        }

                    }

                }

            }

        }

    }

}
