// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

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
    // ── Imperative model management ─────────────────────────────────────
    // Instead of a reactive model binding (which creates a new JS array on
    // every appSettings.layouts change and resets ComboBox popup scroll), we build
    // the model imperatively and only swap it when visible content changes.
    // Match the active filter so the default item is semantically consistent
    // Defer until popup closes

    id: root

    required property var appSettings
    property string noneText: i18n("Default")
    property string currentLayoutId: ""
    property bool showPreview: false
    // Whether to show the "Default"/"None" entry at the top of the list
    property bool showNoneOption: true
    // Filter layouts by category: -1 = show all, 0 = manual/zone only, 1 = autotile only
    property int layoutFilter: -1
    // The layout ID that "Default" actually resolves to at runtime.
    // Set by parent based on context:
    // - Monitor dropdown: appSettings.defaultLayoutId (global default)
    // - Per-desktop dropdown: monitor's layout (or global if none)
    // - Activity dropdown: monitor's layout (or global if none)
    // When layoutFilter === 1 (autotile only), falls back to the global default algorithm.
    property string resolvedDefaultId: {
        if (!appSettings)
            return "";

        if (root.layoutFilter === 1)
            return "autotile:" + appSettings.defaultAutotileAlgorithm;

        return appSettings.defaultLayoutId;
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

    // Helper to find layout by ID.
    // Caches appSettings.layouts locally to avoid repeated QVariant→JS conversion
    // (each appSettings.layouts access creates fresh JS wrappers for all entries).
    function findLayoutById(layoutId) {
        if (!appSettings || !appSettings.layouts || !layoutId)
            return null;

        let layouts = appSettings.layouts;
        for (let i = 0; i < layouts.length; i++) {
            if (layouts[i].id === layoutId)
                return layouts[i];
        }
        return null;
    }

    // Helper to get category with default fallback.
    // Layout objects use `isAutotile` (bool), while overlay/D-Bus objects
    // use `category` (int: 0=Manual, 1=Autotile). Check both fields.
    function getCategory(layout, defaultCategory) {
        if (!layout)
            return defaultCategory;

        if (layout.category !== undefined)
            return layout.category;

        if (layout.isAutotile === true)
            return 1;

        if (layout.isAutotile === false)
            return 0;

        return defaultCategory;
    }

    // Resolve the actual layout that "Default" represents
    function _resolveDefaultLayout() {
        return findLayoutById(resolvedDefaultId);
    }

    function _buildItems() {
        let items = [];
        if (root.showNoneOption) {
            let defaultLayout = _resolveDefaultLayout();
            items.push({
                "text": noneText,
                "value": "",
                "layout": defaultLayout,
                "category": root.layoutFilter >= 0 ? root.layoutFilter : getCategory(defaultLayout, -1),
                "isDefaultOption": true
            });
        }
        if (appSettings && appSettings.layouts) {
            let layouts = appSettings.layouts; // cache locally — avoid repeated QVariant→JS conversion
            let layoutItems = [];
            for (let i = 0; i < layouts.length; i++) {
                let layout = layouts[i];
                let cat = getCategory(layout, 0);
                // Filter by category if layoutFilter is set
                if (root.layoutFilter >= 0 && cat !== root.layoutFilter)
                    continue;

                layoutItems.push({
                    "text": layout.displayName,
                    "value": layout.id,
                    "layout": layout,
                    "category": cat,
                    "isDefaultOption": false
                });
            }
            // Sort: manual (category 0) before dynamic (category 1),
            // alphabetical within each group.
            layoutItems.sort(function (a, b) {
                if (a.category !== b.category)
                    return a.category - b.category;

                return a.text.localeCompare(b.text, undefined, {
                    "sensitivity": "base"
                });
            });
            for (let i = 0; i < layoutItems.length; i++) {
                items.push(layoutItems[i]);
            }
        }
        return items;
    }

    // Compare visible fields only — ignore zone preview data which changes
    // every D-Bus round-trip but doesn't affect what the user sees in the
    // closed combo or the item text/badges. When showPreview is on the mini
    // previews do render zone geometry, so a cheap geometry fingerprint is
    // compared as well — otherwise a geometry-only zone edit would leave the
    // previews stale.
    //
    // Cheap deterministic fingerprint of a layout's zone geometry. Mirrors
    // ZonePreview's field precedence (flat x/y/width/height preferred, nested
    // relativeGeometry as fallback); values are relative (0–1) so rounding to
    // three decimals is stable across D-Bus float round-trips.
    function _zonesFingerprint(layout) {
        let zones = (layout && layout.zones) || [];
        let fp = "";
        for (let i = 0; i < zones.length; i++) {
            let z = zones[i];
            let g = z.relativeGeometry || ({});
            let x = z.x !== undefined ? z.x : (g.x || 0);
            let y = z.y !== undefined ? z.y : (g.y || 0);
            let w = z.width !== undefined ? z.width : (g.width || 0);
            let h = z.height !== undefined ? z.height : (g.height || 0);
            fp += Math.round(x * 1000) + "," + Math.round(y * 1000) + "," + Math.round(w * 1000) + "," + Math.round(h * 1000) + ";";
        }
        return fp;
    }

    function _modelMatchesItems(newItems) {
        if (model.length !== newItems.length)
            return false;

        for (let i = 0; i < model.length; i++) {
            let old = model[i];
            let nw = newItems[i];
            if (old.value !== nw.value)
                return false;

            if (old.text !== nw.text)
                return false;

            if (old.category !== nw.category)
                return false;

            if ((old.layout && old.layout.autoAssign === true) !== (nw.layout && nw.layout.autoAssign === true))
                return false;

            // Aspect ratio class is rendered as a delegate badge, so a
            // change must invalidate the model — otherwise the badge
            // stays stale on the live view even though `appSettings.layouts`
            // reflects the new class.
            if ((old.layout && old.layout.aspectRatioClass) !== (nw.layout && nw.layout.aspectRatioClass))
                return false;

            // Zone count is shown in the popup subtitle and drives the zone
            // preview, so zone add/remove edits must invalidate the model.
            if ((old.layout && old.layout.zoneCount) !== (nw.layout && nw.layout.zoneCount))
                return false;

            // The mini previews render zone geometry, so with the preview
            // visible a geometry-only zone edit must invalidate the model.
            if (root.showPreview && _zonesFingerprint(old.layout) !== _zonesFingerprint(nw.layout))
                return false;

            // For "Default" entry, also check which layout it resolves to
            if (old.isDefaultOption && ((old.layout ? old.layout.id : "") !== (nw.layout ? nw.layout.id : "")))
                return false;
        }
        return true;
    }

    function rebuildModel() {
        if (!_rebuildScheduled) {
            _rebuildScheduled = true;
            Qt.callLater(_doRebuild);
        }
    }

    function _doRebuild() {
        // A coalesced rebuild can fire via Qt.callLater after this ComboBox
        // has been destroyed (fast page-switching); the dying context resolves
        // the component's own methods to undefined. Bail before invoking them
        // rather than throwing "_buildItems is not a function".
        if (typeof _buildItems !== "function")
            return;
        _rebuildScheduled = false;
        let items = _buildItems();
        if (_modelMatchesItems(items)) {
            // Model didn't change visually, but currentLayoutId may have
            // changed while the rebuild was coalesced — always re-sync.
            updateSelection();
            return;
        }
        if (popup && popup.visible) {
            _rebuildPending = true;
            return;
        }
        model = items;
        updateSelection();
    }

    function updateSelection() {
        // Same teardown guard as _doRebuild: a coalesced Qt.callLater(updateSelection)
        // (the onLayoutFilterChanged path) can fire after this ComboBox is
        // destroyed by fast page-switching; the dying context resolves the
        // component's own members to undefined. Bail before touching them.
        if (typeof _buildItems !== "function")
            return;
        // Unmatched id → -1 (no selection), not 0. Coercing to row 0
        // silently rewrites a stale / deleted layout id to whatever
        // happens to be first in the list — when `showNoneOption: false`
        // (used by the rule editor's action pickers) that means the
        // user's saved value gets clobbered the moment they re-open the
        // rule. The empty-id case (no value set) still maps to row 0
        // because row 0 is the "Default"/"None" entry when
        // `showNoneOption: true`; when it's off, leaving currentIndex at
        // -1 also makes "no selection" visible instead of silently
        // committing to the first real entry.
        if (currentLayoutId && currentLayoutId !== "") {
            for (let i = 0; i < model.length; i++) {
                if (model[i].value === currentLayoutId) {
                    currentIndex = i;
                    return;
                }
            }
            currentIndex = -1;
            return;
        }
        currentIndex = showNoneOption ? 0 : -1;
    }

    function clearSelection() {
        currentIndex = 0;
    }

    Accessible.name: i18n("Layout selection")
    textRole: "text"
    valueRole: "value"
    model: []
    onResolvedDefaultIdChanged: rebuildModel()
    onNoneTextChanged: rebuildModel()
    // Filter change (e.g., viewMode switch) completely replaces model content.
    // Rebuild the model synchronously, but do NOT call updateSelection() here.
    // Both layoutFilter and currentLayoutId depend on the same root property
    // (viewMode), and QML evaluates sibling bindings in undefined order.
    // If layoutFilter evaluates first, currentLayoutId still holds the stale
    // value from the previous mode.  onCurrentLayoutIdChanged (below) will
    // fire once that binding settles and call updateSelection() with the
    // correct value against the already-rebuilt model.
    onLayoutFilterChanged: {
        _rebuildScheduled = false;
        let items = _buildItems();
        if (_modelMatchesItems(items))
            return;

        if (popup && popup.visible) {
            _rebuildPending = true;
            return;
        }
        model = items;
        // Defer selection sync until sibling bindings (currentLayoutId) settle.
        // If currentLayoutId also changed, onCurrentLayoutIdChanged handles it
        // first; Qt.callLater deduplicates so this is a no-op in that case.
        // If currentLayoutId did NOT change (both modes have ""), this ensures
        // currentIndex is still correct against the new model.
        Qt.callLater(updateSelection);
    }
    // Update selection when currentLayoutId changes externally.
    onCurrentLayoutIdChanged: updateSelection()
    // Initial build runs synchronously so the model is populated before
    // the first paint frame (Qt.callLater would leave it empty for one frame).
    Component.onCompleted: _doRebuild()

    // Trigger rebuild when data sources change
    Connections {
        function onLayoutsChanged() {
            root.rebuildModel();
        }

        target: root.appSettings ?? null
    }

    // Outside-click closer — mirror of the WideComboBox pattern. While the
    // popup is open, a transparent MouseArea fills the application overlay,
    // closes the popup on any outside press, and consumes the event when
    // the press lands on the ComboBox button itself (so the button can't
    // immediately re-open the popup).
    Loader {
        active: pop.opened
        sourceComponent: catcherComponent
    }

    Component {
        id: catcherComponent

        Item {
            id: catcher

            z: 999998
            Component.onCompleted: {
                const ovr = root.Overlay.overlay;
                if (ovr) {
                    parent = ovr;
                    anchors.fill = ovr;
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                propagateComposedEvents: true
                onPressed: function (mouse) {
                    const rootPos = root.mapToItem(catcher, 0, 0);
                    const onCombo = mouse.x >= rootPos.x && mouse.y >= rootPos.y && mouse.x < rootPos.x + root.width && mouse.y < rootPos.y + root.height;
                    pop.close();
                    mouse.accepted = onCombo;
                }
            }
        }
    }

    // ── Custom popup ────────────────────────────────────────────────────
    // Override the default popup to use a plain ListView instead of the
    // KDE desktop style's Menu-based popup.  The Menu popup has its own
    // internal ListView (bound to contentModel, not delegateModel) which
    // causes positionViewAtIndex to target the wrong view — making the
    // dropdown appear scrolled to the wrong position.
    // Popup follows the WideComboBox z-stacking pattern so the dropdown
    // renders correctly when LayoutComboBox is hosted inside a
    // Kirigami.OverlaySheet (e.g. the unified Rule editor's
    // SetSnappingLayout / SetTilingAlgorithm action editors). Reparenting
    // to Overlay.overlay with a high `z` escapes the sheet's modal layer;
    // `popupType: Popup.Item` keeps it in-scene so we don't trigger the
    // focus-loss regression that closes the host OverlaySheet when a real
    // OS window grabs focus; outside-click dismissal is handled by the
    // Loader-gated catcher below (Qt's `CloseOnPressOutside` is unreliable
    // once the popup is reparented into Overlay.overlay).
    popup: T.Popup {
        id: pop

        popupType: T.Popup.Item
        parent: Overlay.overlay
        z: 999999
        modal: false
        dim: false
        closePolicy: T.Popup.CloseOnEscape
        // Position is set imperatively in `onAboutToShow` — a declarative
        // `mapToItem` binding doesn't re-evaluate when the ComboBox's
        // ancestors move (e.g. while a hosting OverlaySheet animates into
        // place), leaving the popup pinned at overlay-origin (0, 0).
        x: 0
        y: 0
        onAboutToShow: {
            if (parent) {
                const pos = root.mapToItem(parent, 0, root.height);
                x = pos.x;
                y = pos.y;
            }
        }
        width: Math.max(root.width, Kirigami.Units.gridUnit * 18)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (root.Window.window ? root.Window.window.height : 600) - topMargin - bottomMargin)
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing
        padding: 1
        onClosed: {
            if (root._rebuildPending) {
                root._rebuildPending = false;
                root.rebuildModel();
            }
        }

        // The View colorSet is pinned on the contentItem and background
        // individually, NOT on the Popup node: Kirigami's theme attachment
        // resolves through parentItem(), and a QQuickPopup's background /
        // contentItem parent to the internal popup item (→ Overlay.overlay),
        // so a pin on the Popup node never reaches them. Upstream
        // qqc2-desktop-style's ToolTip.qml uses this same per-item pattern.
        contentItem: ListView {
            id: popupList

            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            highlightMoveDuration: 0

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }

        background: Rectangle {
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: Kirigami.Theme.backgroundColor
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }
    }

    // Custom delegate with optional layout preview and category badge
    delegate: ItemDelegate {
        required property var modelData
        required property int index
        readonly property bool hasLayout: modelData.layout != null
        readonly property bool isDefaultOption: modelData.isDefaultOption === true
        readonly property bool isCurrentSelection: root.currentIndex === index

        Accessible.name: modelData.text || ""
        // Reserve the scrollbar's gutter so the row content ends at the
        // scrollbar's left edge instead of running underneath it — otherwise
        // the seam between the full-width delegate and the floating scrollbar
        // shows as a stray vertical line beside the handle (mirrors the
        // FontPickerDialog list pattern).
        width: popupList.width - (popupList.ScrollBar.vertical.visible ? popupList.ScrollBar.vertical.width : 0)
        implicitHeight: Kirigami.Units.gridUnit * 6
        // Only highlight the hovered/keyboard-navigated item (standard ComboBox UX).
        // The current selection is shown with a checkmark, not a second highlight
        // band — two simultaneous highlight bands look like a rendering glitch.
        highlighted: root.highlightedIndex === index

        // Opaque background prevents the ComboBox's closed-state display text
        // from bleeding through the popup delegate (especially the first item).
        // Highlight matches the standard subtle tint used everywhere else (e.g.
        // LayoutGridDelegate): a 0.15-alpha highlightColor wash rather than a
        // full opaque band, so badges and labels stay legible without having to
        // recolor to highlightedTextColor.
        background: Rectangle {
            color: highlighted ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : Kirigami.Theme.backgroundColor
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
                color: Kirigami.Theme.textColor
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
                color: Kirigami.Theme.alternateBackgroundColor
                border.color: highlighted ? Kirigami.Theme.highlightColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                border.width: highlighted ? 2 : 1
                visible: root.showPreview && hasLayout

                PZCommon.ZonePreview {
                    anchors.fill: parent
                    anchors.margins: Math.round(Kirigami.Units.smallSpacing * 0.75)
                    zones: (modelData.layout && modelData.layout.zones) || []
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
                color: Kirigami.Theme.alternateBackgroundColor
                border.color: highlighted ? Kirigami.Theme.highlightColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
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
                        font.weight: (highlighted || isCurrentSelection) ? Font.DemiBold : Font.Normal
                        color: Kirigami.Theme.textColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // Category badge (layout type)
                    PZCommon.CategoryBadge {
                        visible: hasLayout && modelData.category >= 0
                        category: modelData.category
                        autoAssign: modelData.layout && modelData.layout.autoAssign === true
                        globalAutoAssign: root.appSettings && root.appSettings.autoAssignAllLayouts === true
                    }

                    // Autotile capability badges (memory / reflow / script-state)
                    PZCommon.CapabilityBadgeRow {
                        layoutData: modelData.layout || ({})
                    }

                    // Aspect ratio badge
                    PZCommon.AspectRatioBadge {
                        aspectRatioClass: (modelData.layout && modelData.layout.aspectRatioClass) || "any"
                    }
                }

                Label {
                    visible: root.showPreview
                    text: {
                        if (isDefaultOption && !hasLayout) {
                            // "Default"/"None" with no resolution (e.g., quick layout slots)
                            return i18n("No layout assigned");
                        } else if (!hasLayout) {
                            return i18n("No default configured");
                        } else if (isDefaultOption) {
                            let layoutName = (modelData.layout && modelData.layout.displayName) || "";
                            return i18np("→ %2 (%1 zone)", "→ %2 (%1 zones)", (modelData.layout && modelData.layout.zoneCount) || 0, layoutName);
                        } else {
                            return i18np("%1 zone", "%1 zones", (modelData.layout && modelData.layout.zoneCount) || 0);
                        }
                    }
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.textColor
                    opacity: 0.7
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }
}
