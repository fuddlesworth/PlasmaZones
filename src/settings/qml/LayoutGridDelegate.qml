// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import "SearchAnchorHelpers.js" as SearchAnchors

/**
 * @brief Grid delegate for displaying a single layout card
 *
 * Single Responsibility: Render a layout card with thumbnail, info, and interaction.
 */
Item {
    id: root

    required property var modelData
    required property int index
    required property var appSettings
    // Controller exposing the per-layout mutation invokables
    // (setLayoutAutoAssign, setLayoutHidden). Required explicitly rather than
    // read from the implicit context property, matching this file's
    // explicit-dependency policy for everything the delegate touches.
    required property var settingsController
    required property real cellWidth
    required property real cellHeight
    property int viewMode: 0 // 0 = Snapping Layouts, 1 = Auto Tile
    // The full autotile default ID including prefix, for comparison
    readonly property string autotileDefaultId: "autotile:" + root.appSettings.defaultAutotileAlgorithm
    // Global "Auto-assign for all layouts" master toggle (#370). Read once at
    // the root so child controls (auto-assign button, CategoryBadge) share a
    // single binding and stay consistent.
    readonly property bool globalAutoAssign: root.appSettings.autoAssignAllLayouts === true
    // Selection state (bound by the hosting page against its selectedLayoutId)
    property bool isSelected: false
    property bool isHovered: false
    // Inner padding of the card body (border → content). Single-sourced so the
    // corner status/toggle icons — which offset back out past this inset with a
    // negative anchor margin to sit near the card corner — can't drift off-corner
    // if the body inset changes.
    readonly property real _bodyInset: Kirigami.Units.largeSpacing
    // When false, the delegate's right-click context-menu affordance is
    // suppressed entirely (the underlying MouseArea drops RightButton
    // from `acceptedButtons`). Hosts that have no `layoutContextMenu`
    // wired (KCM standalone, preview) set this false to avoid a silent
    // right-click no-op.
    property bool contextMenuEnabled: true

    // Signals
    signal selected(int index)
    signal activated(string layoutId)
    signal deleteRequested(var layout)
    signal contextMenuRequested(var layout)

    width: cellWidth
    height: cellHeight

    // Register a per-layout deep-link anchor ("layout:<id>") with the host
    // page's reveal registry so a layouts search result scrolls to and pulses
    // this exact card (expanding its group card if collapsed). Deferred via
    // callLater so the subtree is attached before the parent-chain walk; mirrors
    // RuleSectionList's per-row registration. No-op when hosted outside a
    // SettingsFlickable (pageFor returns null).
    Component.onCompleted: Qt.callLater(function () {
        // The coalesced callback can fire after this delegate has been
        // destroyed (model churn during fast filtering/reloads); the dying
        // context resolves ids to undefined/null. Bail before dereferencing
        // rather than throwing — same guard as LayoutComboBox._doRebuild.
        if (typeof root === "undefined" || !root)
            return;
        var pg = SearchAnchors.pageFor(root);
        if (pg)
            pg.registerSearchAnchor("layout:" + root.modelData.id, root);
    })
    Component.onDestruction: {
        var pg = SearchAnchors.pageFor(root);
        if (pg)
            pg.unregisterSearchAnchor("layout:" + root.modelData.id);
    }

    Accessible.name: modelData.displayName || i18n("Unnamed Layout")
    Accessible.description: i18np("Layout with %n zone", "Layout with %n zones", modelData.zoneCount || 0)
    Accessible.role: Accessible.ListItem
    Accessible.focusable: true
    // Keyboard reachability for the Keys handlers below (matches
    // WizardTemplateCard): without a tab stop this Item can never gain
    // active focus inside the hosting Flow, making them dead code.
    activeFocusOnTab: true
    // Select before activating, matching the mouse path (single-click selects,
    // double-click delivers selection then activation) so keyboard activation
    // never leaves a stale selection highlight on another card.
    Keys.onReturnPressed: {
        root.selected(root.index);
        root.activated(root.modelData.id);
    }
    // Numpad Enter alias, matching the sibling card components.
    Keys.onEnterPressed: {
        root.selected(root.index);
        root.activated(root.modelData.id);
    }
    // Space selects without activating (WizardTemplateCard/PositionPicker
    // semantics).
    Keys.onSpacePressed: root.selected(root.index)
    Keys.onDeletePressed: {
        // Only system items are undeletable; user (non-system) algorithms are
        // deletable via the context menu, so keyboard Delete matches that path.
        if (!root.modelData.isSystem)
            root.deleteRequested(root.modelData);
    }

    // HoverHandler for hover state — immune to scale transform geometry changes
    // that cause MouseArea.containsMouse to flicker at card boundaries
    HoverHandler {
        id: cardHoverHandler

        onHoveredChanged: root.isHovered = hovered
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: root.contextMenuEnabled ? (Qt.LeftButton | Qt.RightButton) : Qt.LeftButton
        hoverEnabled: false
        onClicked: mouse => {
            // Move active focus to the clicked card so a previously
            // keyboard-focused card doesn't keep the focus ring and receive
            // subsequent Return/Delete presses.
            root.forceActiveFocus();
            if (mouse.button === Qt.RightButton) {
                root.selected(root.index);
                root.contextMenuRequested(root.modelData);
            } else {
                root.selected(root.index);
            }
        }
        onDoubleClicked: mouse => {
            if (mouse.button === Qt.LeftButton)
                root.activated(root.modelData.id);
        }
    }

    Rectangle {
        id: cardBackground

        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        radius: Kirigami.Units.smallSpacing * 1.5
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
        // Neutral surface only — selection/hover fills and borders are
        // painted ONCE by the shared LayoutCard inside the thumbnail (same
        // built-in state chrome the daemon popups use), so the delegate no
        // longer layers its own highlight tints around it.
        color: Kirigami.Theme.backgroundColor
        border.width: root.activeFocus ? 2 : 1
        border.color: {
            // Keyboard-focus indicator (tab stop on the root Item) — kept at
            // the delegate level with Kirigami.Theme.focusColor per the
            // color map; LayoutCard has no keyboard-focus concept.
            if (root.activeFocus)
                return Kirigami.Theme.focusColor;

            // Framework-canonical separator mix (Kirigami.Theme has no
            // separatorColor role; this matches Kirigami.Separator).
            return Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast);
        }

        ColumnLayout {
            anchors.fill: parent
            // Standard inner padding so the content (status icons, thumbnail,
            // footer) doesn't hug the card border. smallSpacing was too tight.
            anchors.margins: root._bodyInset
            spacing: Kirigami.Units.smallSpacing

            // Thumbnail area
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Dim thumbnail when hidden
                opacity: root.modelData.hiddenFromSelector ? 0.5 : 1

                LayoutThumbnail {
                    id: layoutThumbnail

                    // Safe scale calculation - fit thumbnail within parent bounds
                    readonly property real safeImplicitWidth: Math.max(1, implicitWidth)
                    readonly property real safeImplicitHeight: Math.max(1, implicitHeight)
                    readonly property real safeParentWidth: Math.max(1, parent.width)
                    readonly property real safeParentHeight: Math.max(1, parent.height)

                    anchors.centerIn: parent
                    layout: root.modelData
                    isSelected: root.isSelected
                    isHovered: root.isHovered
                    globalAutoAssign: root.globalAutoAssign
                    fontFamily: root.appSettings ? root.appSettings.labelFontFamily : ""
                    fontSizeScale: root.appSettings ? root.appSettings.labelFontSizeScale : 1
                    fontWeight: root.appSettings ? root.appSettings.labelFontWeight : Font.Bold
                    fontItalic: root.appSettings ? root.appSettings.labelFontItalic : false
                    fontUnderline: root.appSettings ? root.appSettings.labelFontUnderline : false
                    fontStrikeout: root.appSettings ? root.appSettings.labelFontStrikeout : false
                    transformOrigin: Item.Center
                    scale: Math.min(1, safeParentWidth / safeImplicitWidth, safeParentHeight / safeImplicitHeight)

                    // Hover the preview to read the layout/algorithm description.
                    // Scoped to the thumbnail graphic (not the whole card) so it
                    // never duels with the corner status-icon / toggle tooltips,
                    // which sit clear of the centred preview. Hidden when the
                    // model carries no description (user layouts often have none).
                    readonly property string descriptionText: root.modelData.description !== undefined ? root.modelData.description : ""

                    HoverHandler {
                        id: thumbnailHover
                    }

                    ToolTip.delay: Kirigami.Units.toolTipDelay
                    ToolTip.visible: thumbnailHover.hovered && layoutThumbnail.descriptionText.length > 0
                    ToolTip.text: layoutThumbnail.descriptionText
                }

                // Top-left indicator row (default star + restriction badge)
                Row {
                    // Status icons use ToolTip.delay instead of HoverHandler to
                    // avoid "mouse grabber ambiguous" warnings.  HoverHandler on
                    // small items inside a parent MouseArea creates competing
                    // hover targets that Qt cannot resolve unambiguously.

                    anchors.top: parent.top
                    anchors.left: parent.left
                    // The content column is inset by _bodyInset, but these corner
                    // status icons read better tucked near the card edge. Offset
                    // back out past the column inset so they sit a small padding
                    // from the card corner, not the thumbnail's.
                    anchors.topMargin: Kirigami.Units.smallSpacing - root._bodyInset
                    anchors.leftMargin: Kirigami.Units.smallSpacing - root._bodyInset
                    spacing: Kirigami.Units.smallSpacing / 2

                    Kirigami.Icon {
                        id: defaultIcon

                        source: "favorite"
                        visible: root.viewMode === 1 ? root.modelData.id === root.autotileDefaultId : root.modelData.id === root.appSettings.defaultLayoutId
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.textColor
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.visible: defaultIconMA.containsMouse && visible
                        ToolTip.text: root.viewMode === 1 ? i18n("Default autotile algorithm") : i18n("Default layout")

                        MouseArea {
                            id: defaultIconMA

                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }
                    }

                    Kirigami.Icon {
                        source: root.modelData.isSystem ? "lock" : "document-edit"
                        visible: root.modelData.isSystem === true || root.modelData.hasSystemOrigin === true
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.disabledTextColor
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.visible: systemIconMA.containsMouse && visible
                        ToolTip.text: {
                            if (root.modelData.isAutotile && root.modelData.isSystem)
                                return i18n("Bundled algorithm");

                            if (root.modelData.isSystem)
                                return i18n("System layout (read-only)");

                            return i18n("Modified system layout");
                        }

                        MouseArea {
                            id: systemIconMA

                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }
                    }

                    Kirigami.Icon {
                        source: "view-filter"
                        visible: {
                            var d = root.modelData;
                            var s = d.allowedScreens;
                            var k = d.allowedDesktops;
                            var a = d.allowedActivities;
                            return (s !== undefined && s !== null && s.length > 0) || (k !== undefined && k !== null && k.length > 0) || (a !== undefined && a !== null && a.length > 0);
                        }
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        color: Kirigami.Theme.disabledTextColor
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.visible: filterIconMA.containsMouse && visible
                        ToolTip.text: i18n("This layout is restricted to specific screens, desktops, or activities")

                        MouseArea {
                            id: filterIconMA

                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }
                    }
                }

                // Top-right toggle buttons (autoAssign and hidden are independent:
                // a layout can be hidden from the zone selector while still auto-assigning
                // new windows when active via screen/desktop/activity assignment)
                Row {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    // See the top-left row: offset back out past the column's
                    // _bodyInset so the toggle buttons sit near the card corner
                    // with a small padding.
                    anchors.topMargin: Kirigami.Units.smallSpacing / 2 - root._bodyInset
                    anchors.rightMargin: Kirigami.Units.smallSpacing / 2 - root._bodyInset
                    spacing: 0

                    // Auto-assign toggle (hidden for autotile — the tiling engine manages those).
                    // When the global "Auto-assign for all layouts" master toggle is on (#370),
                    // every layout effectively auto-assigns regardless of its per-layout flag,
                    // so this button is forced into the on appearance and disabled to make the
                    // override visible (the per-layout flag is preserved underneath).
                    ToolButton {
                        readonly property bool perLayoutAuto: root.modelData.autoAssign === true
                        readonly property bool globalAuto: root.globalAutoAssign
                        readonly property bool effectiveAuto: perLayoutAuto || globalAuto

                        function tooltipText() {
                            if (globalAuto)
                                return i18n("Auto-assign is forced on for all layouts by the global setting (Snapping → Behavior → Window Handling). Turn that off to control this layout individually.");

                            if (perLayoutAuto)
                                return i18n("Auto-assign enabled: new windows fill empty zones. Click to disable.");

                            return i18n("Click to auto-assign new windows to empty zones");
                        }

                        width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                        height: width
                        padding: 0
                        visible: root.modelData.isAutotile !== true && (root.isHovered || effectiveAuto)
                        enabled: !globalAuto
                        icon.name: effectiveAuto ? "window-duplicate" : "window-new"
                        icon.width: Kirigami.Units.iconSizes.small
                        icon.height: Kirigami.Units.iconSizes.small
                        icon.color: effectiveAuto ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                        onClicked: root.settingsController.setLayoutAutoAssign(root.modelData.id, !perLayoutAuto)
                        Accessible.name: i18n("Auto-assign layout")
                        ToolTip.visible: hovered
                        ToolTip.text: tooltipText()
                    }

                    // Visibility toggle
                    ToolButton {
                        width: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                        height: width
                        padding: 0
                        visible: root.isHovered || root.modelData.hiddenFromSelector === true
                        icon.name: root.modelData.hiddenFromSelector ? "view-hidden" : "view-visible"
                        icon.width: Kirigami.Units.iconSizes.small
                        icon.height: Kirigami.Units.iconSizes.small
                        icon.color: root.modelData.hiddenFromSelector ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
                        onClicked: root.settingsController.setLayoutHidden(root.modelData.id, !root.modelData.hiddenFromSelector)
                        Accessible.name: i18n("Toggle layout visibility")
                        ToolTip.visible: hovered
                        ToolTip.text: root.modelData.hiddenFromSelector ? i18n("Hidden from zone selector. Click to show.") : i18n("Visible in zone selector. Click to hide.")
                    }
                }
            }

            // Info row — the category/capability/aspect-ratio badges are now
            // rendered by the shared LayoutCard inside the thumbnail (same
            // badge row the popups show), so only the zone count remains here.
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Kirigami.Units.smallSpacing

                Label {
                    elide: Text.ElideRight
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    text: i18np("%n zone", "%n zones", root.modelData.zoneCount || 0)
                }
            }
        }

        Behavior on border.color {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: Kirigami.Units.shortDuration
            }
        }

        transform: Scale {
            origin.x: cardBackground.width / 2
            origin.y: cardBackground.height / 2
            xScale: root.isHovered ? 1.02 : 1
            yScale: root.isHovered ? 1.02 : 1

            Behavior on xScale {
                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: Kirigami.Units.shortDuration
                }
            }

            Behavior on yScale {
                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: Kirigami.Units.shortDuration
                }
            }
        }
    }
}
