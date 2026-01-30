// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * @brief Reusable combo box for selecting layouts with consistent model building
 *
 * This component eliminates duplication of the layout model building logic
 * across MonitorAssignments, ActivityAssignments, and QuickLayoutSlots.
 *
 * Supports both manual layouts and autotile algorithms (unified layout model).
 * Category: 0 = Manual, 1 = Autotile
 *
 * The "Default" option resolves to the actual default layout for preview.
 */
ComboBox {
    id: root

    required property var kcm
    property string noneText: i18n("Default")
    property string currentLayoutId: ""
    property bool showPreview: false

    // The layout ID that "Default" actually resolves to at runtime.
    // Set by parent based on context:
    // - Monitor dropdown: kcm.defaultLayoutId (global default)
    // - Per-desktop dropdown: monitor's layout (or global if none)
    // - Activity dropdown: monitor's layout (or global if none)
    property string resolvedDefaultId: kcm?.defaultLayoutId ?? ""

    textRole: "text"
    valueRole: "value"

    // Helper to find layout by ID
    function findLayoutById(layoutId) {
        if (!kcm || !kcm.layouts || !layoutId) return null
        for (let i = 0; i < kcm.layouts.length; i++) {
            if (kcm.layouts[i].id === layoutId) {
                return kcm.layouts[i]
            }
        }
        return null
    }

    // Resolve the actual layout that "Default" represents
    readonly property var resolvedDefaultLayout: findLayoutById(resolvedDefaultId)

    model: {
        // First item is "Default" - shows what it actually resolves to
        let defaultLayout = resolvedDefaultLayout
        let items = [{
            text: noneText,
            value: "",
            layout: defaultLayout,  // Show actual resolved layout preview
            category: defaultLayout?.category !== undefined ? defaultLayout.category : -1,
            isDefaultOption: true   // Flag to indicate this is the "Default" option
        }]

        if (kcm && kcm.layouts) {
            for (let i = 0; i < kcm.layouts.length; i++) {
                let layout = kcm.layouts[i]
                items.push({
                    text: layout.name,
                    value: layout.id,
                    layout: layout,
                    category: layout.category !== undefined ? layout.category : 0,
                    isDefaultOption: false
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

    // Custom delegate with optional layout preview and category badge
    delegate: ItemDelegate {
        width: root.popup.width
        highlighted: root.highlightedIndex === index

        required property var modelData
        required property int index

        // Helper properties for readability
        readonly property bool isAutotile: modelData.category === 1
        readonly property bool hasLayout: modelData.layout != null
        readonly property bool isDefaultOption: modelData.isDefaultOption === true

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            // Mini layout preview (only if showPreview is enabled)
            // Match LayoutThumbnail colors for consistency
            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                radius: Kirigami.Units.smallSpacing / 2
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                border.color: highlighted ?
                    Kirigami.Theme.highlightColor :
                    Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
                border.width: highlighted ? 2 : 1
                visible: root.showPreview && hasLayout

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
                            // Match ZonePreview colors for consistency
                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b,
                                highlighted ? 0.45 : 0.25)
                            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.5)
                            border.width: highlighted ? 2 : 1
                            radius: Kirigami.Units.smallSpacing * 0.5
                        }
                    }
                }
            }

            // "None" placeholder - only shown when no default layout is configured
            Rectangle {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                Layout.preferredHeight: Kirigami.Units.gridUnit * 3
                radius: Kirigami.Units.smallSpacing / 2
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
                border.color: highlighted ?
                    Kirigami.Theme.highlightColor :
                    Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
                border.width: highlighted ? 2 : 1
                visible: root.showPreview && !hasLayout

                Kirigami.Icon {
                    anchors.centerIn: parent
                    source: "action-unavailable-symbolic"
                    width: Kirigami.Units.iconSizes.smallMedium
                    height: Kirigami.Units.iconSizes.smallMedium
                    color: Kirigami.Theme.textColor
                    opacity: highlighted ? 0.6 : 0.4
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: modelData.text
                        font.bold: highlighted
                        color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // Category badge (Auto/Manual)
                    QFZCommon.CategoryBadge {
                        visible: hasLayout && modelData.category >= 0
                        category: modelData.category !== undefined ? modelData.category : 0
                    }
                }

                Label {
                    visible: root.showPreview
                    text: {
                        if (isDefaultOption && !hasLayout) {
                            // "Default"/"None" with no resolution (e.g., quick layout slots)
                            return i18n("No layout assigned")
                        } else if (!hasLayout) {
                            return i18n("No default configured")
                        } else if (isDefaultOption) {
                            // For "Default" option, show what it resolves to
                            let layoutName = modelData.layout?.name || ""
                            if (isAutotile) {
                                return i18n("→ %1 (dynamic)", layoutName)
                            } else {
                                return i18n("→ %1 (%2 zones)", layoutName, modelData.layout?.zoneCount || 0)
                            }
                        } else if (isAutotile) {
                            return i18n("Dynamic zones")
                        } else {
                            return i18n("%1 zones", modelData.layout?.zoneCount || 0)
                        }
                    }
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
