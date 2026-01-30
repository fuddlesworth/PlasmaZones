// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable combo box for selecting layouts with consistent model building
 *
 * This component eliminates duplication of the layout model building logic
 * across MonitorAssignments, ActivityAssignments, and QuickLayoutSlots.
 */
ComboBox {
    id: root

    required property var kcm
    property string noneText: i18n("Default")
    property string currentLayoutId: ""
    property bool showPreview: false

    textRole: "text"
    valueRole: "value"

    model: {
        let items = [{text: noneText, value: "", layout: null}]
        if (kcm && kcm.layouts) {
            for (let i = 0; i < kcm.layouts.length; i++) {
                items.push({
                    text: kcm.layouts[i].name,
                    value: kcm.layouts[i].id,
                    layout: kcm.layouts[i]
                })
            }
        }
        return items
    }

    // Update selection when currentLayoutId changes externally
    onCurrentLayoutIdChanged: updateSelection()

    function updateSelection() {
        if (currentLayoutId && currentLayoutId !== "") {
            for (let i = 0; i < model.length; i++) {
                if (model[i].value === currentLayoutId) {
                    currentIndex = i
                    return
                }
            }
        }
        currentIndex = 0
    }

    function clearSelection() {
        currentIndex = 0
    }

    // Custom delegate with optional layout preview
    delegate: ItemDelegate {
        width: root.popup.width
        highlighted: root.highlightedIndex === index

        required property var modelData
        required property int index

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            // Mini layout preview (only if showPreview is enabled)
            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                radius: Kirigami.Units.smallSpacing / 2
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                border.color: highlighted ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                border.width: highlighted ? 2 : 1
                visible: root.showPreview && modelData.layout != null

                Item {
                    id: zonePreviewContainer
                    anchors.fill: parent
                    anchors.margins: Math.round(Kirigami.Units.smallSpacing * 0.75)

                    property var zones: modelData.layout?.zones || []

                    Repeater {
                        model: zonePreviewContainer.zones
                        Rectangle {
                            required property var modelData
                            required property int index
                            property var relGeo: modelData.relativeGeometry || {}
                            x: (relGeo.x || 0) * zonePreviewContainer.width
                            y: (relGeo.y || 0) * zonePreviewContainer.height
                            width: Math.max(2, (relGeo.width || 0.25) * zonePreviewContainer.width)
                            height: Math.max(2, (relGeo.height || 1) * zonePreviewContainer.height)
                            color: highlighted ?
                                Qt.rgba(Kirigami.Theme.highlightedTextColor.r, Kirigami.Theme.highlightedTextColor.g, Kirigami.Theme.highlightedTextColor.b, 0.85) :
                                Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
                            border.color: highlighted ?
                                Kirigami.Theme.highlightedTextColor :
                                Kirigami.Theme.highlightColor
                            border.width: Math.round(Kirigami.Units.devicePixelRatio * 2)
                            radius: Kirigami.Units.smallSpacing * 0.5
                        }
                    }
                }
            }

            // "None" placeholder
            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                radius: Kirigami.Units.smallSpacing / 2
                color: highlighted ?
                    Qt.rgba(Kirigami.Theme.highlightedTextColor.r, Kirigami.Theme.highlightedTextColor.g, Kirigami.Theme.highlightedTextColor.b, 0.15) :
                    Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15)
                border.color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.highlightColor
                border.width: Math.round(Kirigami.Units.devicePixelRatio * 2)
                visible: root.showPreview && modelData.layout == null

                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "action-unavailable-symbolic"
                    width: Kirigami.Units.iconSizes.smallMedium
                    height: Kirigami.Units.iconSizes.smallMedium
                    color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.highlightColor
                    opacity: 0.7
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    text: modelData.text
                    font.bold: highlighted
                    color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Label {
                    visible: root.showPreview
                    text: modelData.layout ? i18n("%1 zones", modelData.layout.zoneCount || 0) : i18n("No layout assigned")
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                    opacity: 0.7
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }

    Component.onCompleted: updateSelection()
}
