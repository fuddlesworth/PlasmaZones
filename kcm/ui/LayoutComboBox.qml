// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * @brief Reusable combo box for selecting layouts with consistent model building
 *
 * This component eliminates duplication of the layout model building logic
 * across MonitorAssignments, ActivityAssignments, and QuickLayoutSlots.
 *
 * Category: 0 = Manual
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

    // Helper to find layout by ID.
    // Caches kcm.layouts locally to avoid repeated QVariant→JS conversion
    // (each kcm.layouts access creates fresh JS wrappers for all entries).
    function findLayoutById(layoutId) {
        if (!kcm || !kcm.layouts || !layoutId) return null
        let layouts = kcm.layouts
        for (let i = 0; i < layouts.length; i++) {
            if (layouts[i].id === layoutId) {
                return layouts[i]
            }
        }
        return null
    }

    // Helper to get category with default fallback.
    // KCM layout objects use `isAutotile` (bool), while overlay/D-Bus objects
    // use `category` (int: 0=Manual, 1=Autotile). Check both fields.
    function getCategory(layout, defaultCategory) {
        if (!layout) return defaultCategory
        if (layout.category !== undefined) return layout.category
        if (layout.isAutotile === true) return 1
        if (layout.isAutotile === false) return 0
        return defaultCategory
    }

    // Resolve the actual layout that "Default" represents
    function _resolveDefaultLayout() {
        return findLayoutById(resolvedDefaultId)
    }

    // ── Imperative model management ─────────────────────────────────────
    // Instead of a reactive model binding (which creates a new JS array on
    // every kcm.layouts change and resets ComboBox popup scroll), we build
    // the model imperatively and only swap it when visible content changes.

    model: []

    function _buildItems() {
        let defaultLayout = _resolveDefaultLayout()
        let items = [{
            text: noneText,
            value: "",
            layout: defaultLayout,
            category: getCategory(defaultLayout, -1),
            isDefaultOption: true
        }]

        if (kcm && kcm.layouts) {
            let layouts = kcm.layouts  // cache locally — avoid repeated QVariant→JS conversion
            let layoutItems = []
            for (let i = 0; i < layouts.length; i++) {
                let layout = layouts[i]
                layoutItems.push({
                    text: layout.name,
                    value: layout.id,
                    layout: layout,
                    category: getCategory(layout, 0),
                    isDefaultOption: false
                })
            }
            // Sort: manual (category 0) before dynamic (category 1),
            // alphabetical within each group.
            layoutItems.sort(function(a, b) {
                if (a.category !== b.category) return a.category - b.category
                return a.text.localeCompare(b.text, undefined, {sensitivity: "base"})
            })
            for (let i = 0; i < layoutItems.length; i++) {
                items.push(layoutItems[i])
            }
        }
        return items
    }

    // Compare visible fields only — ignore zone preview data which changes
    // every D-Bus round-trip but doesn't affect what the user sees in the
    // closed combo or the item text/badges.
    function _modelMatchesItems(newItems) {
        if (model.length !== newItems.length) return false
        for (let i = 0; i < model.length; i++) {
            let old = model[i]
            let nw = newItems[i]
            if (old.value !== nw.value) return false
            if (old.text !== nw.text) return false
            if (old.category !== nw.category) return false
            if ((old.layout?.autoAssign === true) !== (nw.layout?.autoAssign === true)) return false
            // For "Default" entry, also check which layout it resolves to
            if (old.isDefaultOption && (old.layout?.id ?? "") !== (nw.layout?.id ?? "")) return false
        }
        return true
    }

    // Defer model swap while the popup is open to prevent scroll resets.
    // The model will update as soon as the popup closes.
    property bool _rebuildPending: false

    // ── Coalesced rebuild ────────────────────────────────────────────────
    // Multiple signals (layoutsChanged, resolvedDefaultIdChanged, etc.)
    // can fire in the same frame.  Qt.callLater batches them into a single
    // _buildItems() + _modelMatchesItems() pass, cutting work by ~Nx where
    // N is the number of signals that arrive per frame (typically 2-3).
    property bool _rebuildScheduled: false

    function rebuildModel() {
        if (!_rebuildScheduled) {
            _rebuildScheduled = true
            Qt.callLater(_doRebuild)
        }
    }

    function _doRebuild() {
        _rebuildScheduled = false
        let items = _buildItems()
        if (_modelMatchesItems(items)) {
            // Model didn't change visually, but currentLayoutId may have
            // changed while the rebuild was coalesced — always re-sync.
            updateSelection()
            return
        }
        if (popup && popup.visible) {
            _rebuildPending = true
            return // Defer until popup closes
        }
        model = items
        updateSelection()
    }

    // ── Custom popup ────────────────────────────────────────────────────
    // Override the default popup to use a plain ListView instead of the
    // KDE desktop style's Menu-based popup.  The Menu popup has its own
    // internal ListView (bound to contentModel, not delegateModel) which
    // causes positionViewAtIndex to target the wrong view — making the
    // dropdown appear scrolled to the wrong position.
    popup: T.Popup {
        y: root.height
        width: Math.max(root.width, Kirigami.Units.gridUnit * 18)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         (root.Window.window ? root.Window.window.height : 600)
                          - topMargin - bottomMargin)
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing
        padding: 1

        onClosed: {
            if (root._rebuildPending) {
                root._rebuildPending = false
                root.rebuildModel()
            }
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
        }

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor
            border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                                  Kirigami.Theme.textColor.g,
                                  Kirigami.Theme.textColor.b, 0.2)
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }
    }

    // Trigger rebuild when data sources change
    Connections {
        target: root.kcm ?? null
        function onLayoutsChanged() { root.rebuildModel() }
    }
    onResolvedDefaultIdChanged: rebuildModel()
    onNoneTextChanged: rebuildModel()

    // Update selection when currentLayoutId changes externally.
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
        width: root.popup.availableWidth
        implicitHeight: Kirigami.Units.gridUnit * 6
        // Only highlight the hovered/keyboard-navigated item (standard ComboBox UX).
        // The current selection is shown with a checkmark, not a second highlight
        // band — two simultaneous highlight bands look like a rendering glitch.
        highlighted: root.highlightedIndex === index

        required property var modelData
        required property int index

        readonly property bool hasLayout: modelData.layout != null
        readonly property bool isDefaultOption: modelData.isDefaultOption === true
        readonly property bool isCurrentSelection: root.currentIndex === index

        // Opaque background prevents the ComboBox's closed-state display text
        // from bleeding through the popup delegate (especially the first item).
        background: Rectangle {
            color: highlighted ? Kirigami.Theme.highlightColor :
                   isCurrentSelection ? Qt.rgba(Kirigami.Theme.highlightColor.r,
                                                Kirigami.Theme.highlightColor.g,
                                                Kirigami.Theme.highlightColor.b, 0.15) :
                   Kirigami.Theme.backgroundColor
        }

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            // Checkmark for the currently selected item
            Kirigami.Icon {
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                Layout.alignment: Qt.AlignVCenter
                source: "checkmark"
                visible: isCurrentSelection
                color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            }
            // Spacer when no checkmark — keeps text aligned
            Item {
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                visible: !isCurrentSelection
            }

            // Mini layout preview (only if showPreview is enabled)
            // Uses shared ZonePreview component
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

                QFZCommon.ZonePreview {
                    anchors.fill: parent
                    anchors.margins: Math.round(Kirigami.Units.smallSpacing * 0.75)
                    zones: modelData.layout?.zones || []
                    isHovered: highlighted
                    showZoneNumbers: false
                    minZoneSize: 2
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
                        font.bold: highlighted || isCurrentSelection
                        color: highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // Category badge (layout type)
                    QFZCommon.CategoryBadge {
                        visible: hasLayout && modelData.category >= 0
                        category: modelData.category
                        autoAssign: modelData.layout?.autoAssign === true
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
                            let layoutName = modelData.layout?.name || ""
                            return i18n("→ %1 (%2 zones)", layoutName, modelData.layout?.zoneCount || 0)
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

    // Initial build runs synchronously so the model is populated before
    // the first paint frame (Qt.callLater would leave it empty for one frame).
    Component.onCompleted: _doRebuild()
}
