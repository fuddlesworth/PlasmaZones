// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Shared layout card for rendering a single layout in grid/list views.
 *
 * Used by ZoneSelectorWindow (drag zone selection) and LayoutPickerOverlay
 * (keyboard/mouse layout picker). Feature flags control mode-specific elements;
 * visual constants (alphas, radii, spacing) are shared via the `style` QtObject.
 *
 * MouseArea is NOT included — each parent provides its own interaction model.
 */
Item {
    id: root

    // Card grid is embedded in settings pages — use View roles for surfaces
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    // Data
    property var layoutData: ({})
    property bool isActive: false
    property bool isSelected: false
    property bool isHovered: false
    property bool isRecommended: (layoutData && layoutData.recommended !== undefined) ? layoutData.recommended : true
    // When the global "Auto-assign for all layouts" master toggle is on (#370),
    // every layout effectively auto-assigns. Parents pass it down so the badge
    // reflects actual snap behavior even when the per-layout flag is off.
    property bool globalAutoAssign: false
    // Dimensions (set by parent, no defaults)
    required property real previewWidth
    required property real previewHeight
    // Feature toggles
    property bool showCardBackground: false
    // ZonePreview passthrough
    property int selectedZoneIndex: -1
    property int zonePadding: 2
    property int edgeGap: 2
    property int minZoneSize: 10
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    property bool showZoneNumbers: true
    property string zoneNumberDisplay: "all"
    property bool producesOverlappingZones: false
    /// ZonePreview's strip axis ticks: "none", "horizontal" or "vertical".
    /// Only the scrolling surfaces set it; every layout host leaves the
    /// default and draws no ticks.
    property string stripAxisHint: "none"
    /// Non-empty replaces the zone preview in the well with the shared
    /// StripEmptyState: the axis arrow plus this caption. Only the scrolling
    /// hosts set it, and only when the strip has nothing to draw.
    ///
    /// It lives INSIDE the card rather than replacing the card, so an empty
    /// strip keeps its name row and category badge. A screen whose strip is
    /// empty still has a template governing it, and swapping the whole card
    /// out for a bare well would stop naming it exactly when the user is most
    /// likely to be wondering which one is in force.
    ///
    /// Separate from placeholderIcon: that stands for an entry which IS an
    /// absence (the no-layout row), while this is a real strip that currently
    /// holds nothing. The two never apply at once.
    property string stripEmptyCaption: ""
    property color zoneHighlightColor: ZoneColorDefaults.previewActiveZoneColor
    property color zoneInactiveColor: ZoneColorDefaults.previewInactiveZoneColor
    property color zoneBorderColor: ZoneColorDefaults.previewZoneBorderColor
    /// Icon name drawn centered in the preview well INSTEAD of the zones,
    /// for an entry that stands for the absence of a layout rather than for
    /// a layout. Empty (the default) keeps the ordinary zone rendering.
    ///
    /// Such an entry has no zones to draw, and an empty well reads as a card
    /// that failed to load rather than as a deliberate "none of these". The
    /// caller names the icon so the shared card does not have to know which
    /// kind of absence it is describing.
    property string placeholderIcon: ""
    // Autotile algorithm metadata
    property bool showMasterDot: false
    /// Number of master zones to mark with indicator dots (ZonePreview
    /// passthrough — the settings thumbnails show N dots for multi-master
    /// algorithms; the popups keep the default of 1).
    property int masterCount: 1
    // Theme colors
    property color highlightColor: Kirigami.Theme.highlightColor
    property color textColor: Kirigami.Theme.textColor
    property color backgroundColor: Kirigami.Theme.backgroundColor
    // Font passthrough
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    // Kirigami-duration passthroughs bound to `durationOverride` on this
    // file's Behavior animations (see usages below). The profile registry
    // supplies the curve shape; these supply the theme-scaled timing so
    // Plasma's system animation-speed preference still applies. Consumers
    // (`LayoutPickerContent.qml`, `ZoneSelectorContent.qml`) override per-instance.
    property int animationDuration: Kirigami.Units.longDuration
    property int shortAnimationDuration: Kirigami.Units.shortDuration
    // Label
    property real labelTopMargin: Kirigami.Units.smallSpacing * 2
    // Computed state colors — single source of truth for both rects
    readonly property color stateHighlightFill: {
        if (root.isActive)
            return Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, style.fillActive);

        if (root.isSelected)
            return Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, style.fillSelected);

        if (root.isHovered)
            return Qt.alpha(Kirigami.Theme.hoverColor, style.hoverTint);

        // Alpha-0 version of the adjacent hover tint (not "transparent",
        // which is alpha-0 BLACK and drags the fade's RGB toward black).
        return Qt.alpha(Kirigami.Theme.hoverColor, 0);
    }
    readonly property color stateBorderColor: {
        if (root.isActive)
            return Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, style.borderActive);

        if (root.isSelected)
            return Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, style.borderSelected);

        // Alpha-0 version of the highlight hue both branches above fade
        // from (not "transparent", which interpolates RGB toward black).
        return Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, 0);
    }
    readonly property int stateBorderWidth: root.isActive ? style.borderWide : (root.isSelected ? style.borderNarrow : 0)

    // Dim non-recommended layouts (different aspect ratio class than current screen)
    opacity: root.isRecommended ? 1 : 0.65
    // Accessibility
    Accessible.role: Accessible.Pane
    Accessible.name: root.layoutData ? (root.layoutData.displayName || "") : ""

    // Visual constants
    QtObject {
        id: style

        // Unified state-based fill alphas (same palette for both modes)
        readonly property real fillActive: 0.12
        readonly property real fillSelected: 0.1
        readonly property real hoverTint: 0.2
        // Unified state-based border alphas
        readonly property real borderActive: 0.5
        readonly property real borderSelected: 0.4
        // Border widths
        readonly property int borderWide: 2
        readonly property int borderNarrow: 1
        // Label
        readonly property real labelDimOpacity: 0.8
        // Badge ratios
        readonly property real checkmarkFontRatio: 0.6
        // Radii
        readonly property real cardRadius: Kirigami.Units.gridUnit
        readonly property real previewRadius: Kirigami.Units.smallSpacing * 1.5
    }

    // Card background (visible in card mode — tints whole card)
    Rectangle {
        id: cardBackground

        anchors.fill: parent
        visible: root.showCardBackground
        radius: style.cardRadius
        color: root.stateHighlightFill
        border.color: root.stateBorderColor
        border.width: root.stateBorderWidth

        Behavior on color {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: root.animationDuration
            }
        }

        Behavior on border.color {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: root.animationDuration
            }
        }

        Behavior on border.width {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: root.shortAnimationDuration
            }
        }
    }

    // Preview area — bounding box for the layout preview.
    // The actual preview rect inside may be smaller to match the layout's
    // intended aspect ratio (letterboxed/pillarboxed within the bounds).
    Item {
        id: previewArea

        // Compute preview rect dimensions fitted to the layout's aspect ratio
        // within the previewWidth × previewHeight bounding box.
        readonly property real layoutAR: {
            var cls = root.layoutData ? (root.layoutData.aspectRatioClass || "any") : "any";
            switch (cls) {
            case "standard":
                return 16 / 9;
            case "ultrawide":
                return 21 / 9;
            case "super-ultrawide":
                return 32 / 9;
            case "portrait":
                return 9 / 16;
            default:
                // "any" — fill the bounding box (use bounding box AR)
                return root.previewHeight > 0 ? root.previewWidth / root.previewHeight : 16 / 9;
            }
        }
        readonly property real boundsAR: root.previewHeight > 0 ? root.previewWidth / root.previewHeight : 16 / 9
        // Fit: if layout is wider than bounds, width-constrain; otherwise height-constrain
        readonly property real fittedWidth: layoutAR > boundsAR ? root.previewWidth : Math.round(root.previewHeight * layoutAR)
        readonly property real fittedHeight: layoutAR > boundsAR ? Math.round(root.previewWidth / layoutAR) : root.previewHeight

        anchors.top: parent.top
        anchors.topMargin: root.showCardBackground ? Kirigami.Units.gridUnit : 0
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.previewWidth
        height: root.previewHeight

        // State-responsive in non-card mode; neutral tint in card mode
        Rectangle {
            id: previewBackground

            anchors.centerIn: parent
            width: previewArea.fittedWidth
            height: previewArea.fittedHeight
            radius: style.previewRadius
            color: root.showCardBackground ? Kirigami.Theme.alternateBackgroundColor : root.stateHighlightFill
            border.color: root.showCardBackground ? "transparent" : root.stateBorderColor
            border.width: root.showCardBackground ? 0 : root.stateBorderWidth

            Behavior on color {
                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: root.animationDuration
                }
            }

            Behavior on border.color {
                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: root.animationDuration
                }
            }

            Behavior on border.width {
                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: root.shortAnimationDuration
                }
            }
        }

        // Placeholder icon, drawn in the well an absence-entry has no zones
        // to fill. Sized off the well rather than the card so it scales with
        // the preview like the zones it stands in for, and tinted with the
        // card's own text color at reduced opacity so it reads as a
        // placeholder rather than as content.
        Kirigami.Icon {
            anchors.centerIn: previewBackground
            visible: root.placeholderIcon !== ""
            source: root.placeholderIcon
            width: Math.round(Math.min(previewBackground.width, previewBackground.height) * 0.4)
            height: width
            color: root.textColor
            opacity: 0.7
        }

        // Active checkmark badge (top-right)
        Rectangle {
            id: activeBadge

            readonly property int badgeSize: root.showCardBackground ? Math.round(root.previewWidth * 0.14) : Math.round(Kirigami.Units.gridUnit * 2.5)

            anchors.right: previewBackground.right
            anchors.top: previewBackground.top
            anchors.rightMargin: Kirigami.Units.smallSpacing
            anchors.topMargin: Kirigami.Units.smallSpacing
            width: root.isActive ? badgeSize : 0
            height: root.isActive ? badgeSize : 0
            radius: badgeSize / 2
            color: Kirigami.Theme.highlightColor
            opacity: root.isActive ? 1 : 0
            z: 10

            Label {
                anchors.centerIn: parent
                text: "\u2713"
                font.pixelSize: Math.round(activeBadge.badgeSize * style.checkmarkFontRatio)
                font.bold: true
                color: Kirigami.Theme.highlightedTextColor
                visible: root.isActive
            }

            Behavior on width {
                PhosphorMotionAnimation {
                    profile: root.isActive ? "widget.badgeShow" : "widget.badgeHide"
                    durationOverride: root.animationDuration
                }
            }

            Behavior on height {
                PhosphorMotionAnimation {
                    profile: root.isActive ? "widget.badgeShow" : "widget.badgeHide"
                    durationOverride: root.animationDuration
                }
            }

            // Opacity must not overshoot — badgeShow's curve has overshoot
            // for the size pop, but for opacity that produces a clamped peak.
            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: root.isActive ? "widget.fadeIn" : "widget.fadeOut"
                    durationOverride: root.shortAnimationDuration
                }
            }
        }

        // Zone rectangles — fill the fitted preview background, not the bounding box
        ZonePreview {
            id: zonePreview

            anchors.fill: previewBackground
            anchors.margins: root.showCardBackground ? Kirigami.Units.smallSpacing : 0
            // Hidden rather than fed an empty list: an empty ZonePreview still
            // draws its own axis ticks, which would double up with the empty
            // state's arrow in the same well.
            visible: root.stripEmptyCaption === ""
            zones: root.layoutData ? (root.layoutData.zones || []) : []
            showZoneNumbers: root.showZoneNumbers
            zoneNumberDisplay: root.zoneNumberDisplay
            producesOverlappingZones: root.producesOverlappingZones
            selectedZoneIndex: root.selectedZoneIndex
            isHovered: root.isHovered || root.isSelected
            isActive: root.isActive
            zonePadding: root.zonePadding
            edgeGap: root.edgeGap
            minZoneSize: root.minZoneSize
            highlightColor: root.zoneHighlightColor
            inactiveColor: root.zoneInactiveColor
            borderColor: root.zoneBorderColor
            inactiveOpacity: root.inactiveOpacity
            activeOpacity: root.activeOpacity
            fontFamily: root.fontFamily
            fontSizeScale: root.fontSizeScale
            fontWeight: root.fontWeight
            fontItalic: root.fontItalic
            fontUnderline: root.fontUnderline
            fontStrikeout: root.fontStrikeout
            showMasterDot: root.showMasterDot
            masterCount: root.masterCount
            stripAxisHint: root.stripAxisHint
            animationDuration: root.animationDuration
        }

        // The empty strip, in the same well the zones would have filled.
        StripEmptyState {
            anchors.fill: previewBackground
            anchors.margins: root.showCardBackground ? Kirigami.Units.smallSpacing : 0
            visible: root.stripEmptyCaption !== ""
            verticalAxis: root.stripAxisHint === "vertical"
            caption: root.stripEmptyCaption
            contentColor: root.textColor
        }
    }

    // Name label row
    Row {
        id: nameLabelRow

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: previewArea.bottom
        anchors.topMargin: root.labelTopMargin
        spacing: Kirigami.Units.smallSpacing

        CategoryBadge {
            id: categoryBadge

            anchors.verticalCenter: parent.verticalCenter
            category: (root.layoutData && root.layoutData.category !== undefined) ? root.layoutData.category : 0
            autoAssign: root.layoutData ? root.layoutData.autoAssign === true : false
            globalAutoAssign: root.globalAutoAssign
        }

        CapabilityBadgeRow {
            id: capabilityBadges

            anchors.verticalCenter: parent.verticalCenter
            layoutData: root.layoutData || ({})
        }

        AspectRatioBadge {
            id: aspectRatioBadge

            anchors.verticalCenter: parent.verticalCenter
            aspectRatioClass: root.layoutData ? (root.layoutData.aspectRatioClass || "any") : "any"
        }

        Label {
            id: nameLabel

            anchors.verticalCenter: parent.verticalCenter
            text: root.layoutData ? (root.layoutData.displayName || "") : ""
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 1
            font.weight: root.isActive ? Font.Bold : Font.Normal
            color: {
                // Strip the pipeline colour's carried alpha — a 50%-alpha
                // highlight must not render a 50%-opacity title.
                if (root.isActive)
                    return Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, 1);

                if (root.isSelected || root.isHovered)
                    return root.textColor;

                return Kirigami.Theme.disabledTextColor;
            }
            // Width the visible badge siblings occupy in the Row, including
            // the Row spacing each contributes before the label. Row only
            // spaces VISIBLE items, so hidden badges cost nothing here either.
            readonly property real badgesWidth: {
                var w = 0;
                if (categoryBadge.visible)
                    w += categoryBadge.width + nameLabelRow.spacing;
                if (capabilityBadges.visible)
                    w += capabilityBadges.width + nameLabelRow.spacing;
                if (aspectRatioBadge.visible)
                    w += aspectRatioBadge.width + nameLabelRow.spacing;
                return w;
            }

            opacity: (root.isSelected || root.isHovered || root.isActive) ? 1 : style.labelDimOpacity
            elide: Text.ElideRight
            maximumLineCount: 1
            // Cap against the preview width minus the badges sharing the Row,
            // so the horizontalCenter-anchored row can't spill past the card
            // edges when badges are visible.
            width: Math.min(implicitWidth, Math.max(0, root.previewWidth - Kirigami.Units.gridUnit - badgesWidth))

            Behavior on color {
                PhosphorMotionAnimation {
                    profile: "widget.tint"
                    durationOverride: root.animationDuration
                }
            }

            Behavior on opacity {
                PhosphorMotionAnimation {
                    // Direction is taken from the same predicate driving
                    // the label's `opacity` binding above so the leg is
                    // decided when the highlight state flips, rather than
                    // re-evaluating on every interpolated `opacity` tick.
                    profile: (root.isSelected || root.isHovered || root.isActive) ? "widget.fadeIn" : "widget.fadeOut"
                    durationOverride: root.animationDuration
                }
            }
        }
    }
}
