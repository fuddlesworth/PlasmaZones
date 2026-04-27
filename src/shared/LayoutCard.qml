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

    // Data
    property var layoutData: ({
    })
    property bool isActive: false
    property bool isSelected: false
    property bool isHovered: false
    property bool isRecommended: layoutData.recommended !== undefined ? layoutData.recommended : true
    // When the global "Auto-assign for all layouts" master toggle is on (#370),
    // every layout effectively auto-assigns. Parents pass it down so the badge
    // reflects actual snap behavior even when the per-layout flag is off.
    property bool globalAutoAssign: false
    // Dimensions (set by parent, no defaults)
    property real previewWidth
    property real previewHeight
    // Feature toggles
    property bool showCardBackground: false
    // ZonePreview passthrough
    property bool interactive: false
    property int selectedZoneIndex: -1
    property int zonePadding: 2
    property int edgeGap: 2
    property int minZoneSize: 10
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    property bool showZoneNumbers: true
    property string zoneNumberDisplay: "all"
    property bool producesOverlappingZones: false
    property color zoneHighlightColor: Kirigami.Theme.highlightColor
    property color zoneInactiveColor: Kirigami.Theme.textColor
    property color zoneBorderColor: Kirigami.Theme.textColor
    property real hoverScale: 1
    // Autotile algorithm metadata
    property bool showMasterDot: false
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
    // DEPRECATED — vestigial Kirigami-duration passthroughs. Pre-PR-344
    // QML animations referenced these to override Behavior durations
    // per-instance; post-migration, durations come from the profile
    // registry (see `PhosphorAnimation::ProfilePaths::Widget*`). Kept as
    // no-op accepts so existing consumer QML (`AlgorithmPreview.qml`,
    // `GeneralPage.qml`) does not fail to parse.
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
            return Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, style.fillHovered);

        return "transparent";
    }
    readonly property color stateBorderColor: {
        if (root.isActive)
            return Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, style.borderActive);

        if (root.isSelected)
            return Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, style.borderSelected);

        return "transparent";
    }
    readonly property int stateBorderWidth: root.isActive ? style.borderWide : (root.isSelected ? style.borderNarrow : 0)

    // Signals
    signal zoneHovered(int zoneIndex)

    // Dim non-recommended layouts (different aspect ratio class than current screen)
    opacity: root.isRecommended ? 1 : 0.65
    // Accessibility
    Accessible.role: Accessible.Pane
    Accessible.name: root.layoutData.name || ""

    // Visual constants
    QtObject {
        id: style

        // Unified state-based fill alphas (same palette for both modes)
        readonly property real fillActive: 0.12
        readonly property real fillSelected: 0.1
        readonly property real fillHovered: 0.06
        readonly property real fillNeutral: 0.08
        // Unified state-based border alphas
        readonly property real borderActive: 0.5
        readonly property real borderSelected: 0.4
        // Border widths
        readonly property int borderWide: 2
        readonly property int borderNarrow: 1
        // Label
        readonly property real labelDimAlpha: 0.6
        readonly property real labelDimOpacity: 0.8
        // Badge ratios
        readonly property real checkmarkFontRatio: 0.6
        // Animation
        readonly property real badgeOvershoot: 1.5
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
            color: root.showCardBackground ? Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, style.fillNeutral) : root.stateHighlightFill
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
                    profile: "widget.badge"
                    durationOverride: root.animationDuration
                }

            }

            Behavior on height {
                PhosphorMotionAnimation {
                    profile: "widget.badge"
                    durationOverride: root.animationDuration
                }

            }

            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: "widget.badge"
                    durationOverride: root.shortAnimationDuration
                }

            }

        }

        // Zone rectangles — fill the fitted preview background, not the bounding box
        ZonePreview {
            id: zonePreview

            anchors.fill: previewBackground
            anchors.margins: root.showCardBackground ? Kirigami.Units.smallSpacing : 0
            zones: root.layoutData.zones || []
            interactive: root.interactive
            showZoneNumbers: root.showZoneNumbers
            zoneNumberDisplay: root.zoneNumberDisplay
            producesOverlappingZones: root.producesOverlappingZones
            highlightAllZones: false
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
            hoverScale: root.hoverScale
            fontFamily: root.fontFamily
            fontSizeScale: root.fontSizeScale
            fontWeight: root.fontWeight
            fontItalic: root.fontItalic
            fontUnderline: root.fontUnderline
            fontStrikeout: root.fontStrikeout
            showMasterDot: root.showMasterDot
            animationDuration: root.animationDuration
            onZoneHovered: function(index) {
                root.zoneHovered(index);
            }
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
            anchors.verticalCenter: parent.verticalCenter
            category: root.layoutData.category !== undefined ? root.layoutData.category : 0
            autoAssign: root.layoutData.autoAssign === true
            globalAutoAssign: root.globalAutoAssign
        }

        AspectRatioBadge {
            anchors.verticalCenter: parent.verticalCenter
            aspectRatioClass: root.layoutData.aspectRatioClass || "any"
        }

        Label {
            id: nameLabel

            anchors.verticalCenter: parent.verticalCenter
            text: root.layoutData.name || ""
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize + 1
            font.weight: root.isActive ? Font.Bold : Font.Normal
            color: {
                if (root.isActive)
                    return root.highlightColor;

                if (root.isSelected || root.isHovered)
                    return root.textColor;

                return Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, style.labelDimAlpha);
            }
            opacity: (root.isSelected || root.isHovered || root.isActive) ? 1 : style.labelDimOpacity
            elide: Text.ElideRight
            maximumLineCount: 1
            width: Math.min(implicitWidth, root.previewWidth - Kirigami.Units.gridUnit)

            Behavior on color {
                PhosphorMotionAnimation {
                    profile: "widget.fade"
                    durationOverride: root.animationDuration
                }

            }

            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: "widget.fade"
                    durationOverride: root.animationDuration
                }

            }

        }

    }

}
