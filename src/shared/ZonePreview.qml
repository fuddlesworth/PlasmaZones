// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Shared zone preview component for rendering layout zones
 *
 * Used by:
 * - KCM LayoutThumbnail for settings page previews
 * - ZoneSelectorWindow for drag-and-drop zone selection
 * - LayoutPreview for layout selection popup
 *
 * Renders zones with consistent styling, gaps, numbers, and theming.
 */
Item {
    id: root

    // Settings-embedded preview — resolve theme roles against the View set
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    /// Array of zone objects with relativeGeometry: { x, y, width, height }
    required property var zones
    /// Whether this preview is in "active/selected" state (affects coloring)
    property bool isActive: false
    /// Whether this preview is hovered
    property bool isHovered: false
    /// Index of the currently selected zone (-1 for none)
    property int selectedZoneIndex: -1
    /// Gap between zones in pixels (applied as zonePadding/2 per side between adjacent zones)
    property real zonePadding: 1
    /// Gap at screen edges in pixels
    property real edgeGap: 1
    /// Minimum zone size in pixels
    property int minZoneSize: 8
    /// Whether to show zone numbers
    property bool showZoneNumbers: true
    /// How to display zone numbers: "all", "first", "last", "firstAndLast", "none"
    property string zoneNumberDisplay: "all"
    /// Whether zones overlap by design (monocle, cascade, etc.).
    /// When true, edge gaps and zone padding are skipped so the algorithm's
    /// raw geometry is rendered as-is. Set from algorithm metadata
    /// (@producesOverlappingZones) rather than auto-detected at runtime.
    property bool producesOverlappingZones: false
    /// Animation duration in ms — bound to `durationOverride` on this
    /// file's Behavior animations (see usages below). The profile registry
    /// supplies the curve shape; this supplies the theme-scaled timing so
    /// Plasma's system animation-speed preference still applies. Consumers
    /// (`LayoutCard.qml`, `AlgorithmPreview.qml`) override per-instance.
    property int animationDuration: Kirigami.Units.shortDuration
    /// Whether zone click/hover signals are enabled (disable for thumbnail use)
    property bool interactive: false
    /// Whether all zones highlight together when any is selected
    property bool highlightAllZones: false
    /// Array of zone IDs to highlight (for navigation OSD zone highlighting)
    property var highlightedZoneIds: []
    /// Whether specific zones are singled out, by index or by zone ID. The
    /// card-level `isActive` / `isHovered` states must not light every zone
    /// while this holds, or the singled-out zone renders identically to its
    /// siblings and the selection is invisible. Consumers that pass no per-zone
    /// selection (layout picker, OSD, settings thumbnails) keep the whole-card
    /// highlight. `highlightAllZones` still lights every zone, via
    /// `isZoneSelected` below.
    readonly property bool hasZoneSelection: root.selectedZoneIndex >= 0 || (root.highlightedZoneIds && root.highlightedZoneIds.length > 0)
    /// Zone fill opacity when not active/hovered
    property real inactiveOpacity: 0.25
    /// Zone fill opacity when active/hovered
    property real activeOpacity: 0.45
    /// Highlight color for selected zones (default: shared View-set highlight)
    property color highlightColor: ZoneColorDefaults.previewActiveZoneColor
    /// Inactive color for non-selected zones (default: shared View-set surface)
    property color inactiveColor: ZoneColorDefaults.previewInactiveZoneColor
    /// Border color (default: shared View-set separator)
    property color borderColor: ZoneColorDefaults.previewZoneBorderColor
    /// Label font color for zone numbers (default: theme text)
    property color labelFontColor: Kirigami.Theme.textColor
    /// Scale factor when zone is hovered (1.0 = no scale, set > 1.0 to enable)
    property real hoverScale: 1
    /// Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    /// Whether to show master indicator dots on master zone(s)
    property bool showMasterDot: false
    /// Number of master zones to mark with indicator dots
    property int masterCount: 1

    /// Emitted when a zone is hovered
    signal zoneHovered(int index)

    Repeater {
        model: root.zones || []

        delegate: Rectangle {
            id: zoneRect

            // The zone selector hit-tests the cursor in C++ by reading these
            // delegates' rendered geometry (selector.cpp::updateSelectorPosition)
            // rather than replaying the layout math below. Renaming this breaks
            // per-zone highlighting silently, so it is part of the contract.
            objectName: "zonePreviewZone"

            required property var modelData
            required property int index
            // Parse relative geometry — clamp to [0, 1] to handle fixed-geometry
            // layouts whose reference screen differs from current (zones can exceed 1.0).
            // Zones may come from LayoutPreview (flat x/y/w/h) or the legacy
            // zonesToVariantList shape (nested relativeGeometry); prefer flat,
            // fall back to nested.
            property var relGeo: modelData.relativeGeometry || ({})
            property real relX: Math.max(0, Math.min((modelData.x !== undefined ? modelData.x : (relGeo.x || 0)), 1))
            property real relY: Math.max(0, Math.min((modelData.y !== undefined ? modelData.y : (relGeo.y || 0)), 1))
            property real relWidth: Math.max(0, Math.min((modelData.width !== undefined ? modelData.width : (relGeo.width || 0.25)), 1 - relX))
            property real relHeight: Math.max(0, Math.min((modelData.height !== undefined ? modelData.height : (relGeo.height || 1)), 1 - relY))
            // Check if this zone is selected (by index, highlightAllZones, or by zone ID)
            property bool isZoneSelected: {
                // Highlight all zones when any is selected (highlightAllZones mode)
                if (root.highlightAllZones && root.selectedZoneIndex >= 0)
                    return true;

                // Option 2: Highlight by index (layout selector mode)
                if (root.selectedZoneIndex === index)
                    return true;

                // Option 3: Highlight by zone ID (navigation OSD mode)
                // Note: QStringList from C++ becomes QVariantList in QML, so we need
                // to iterate and compare strings explicitly (indexOf may not work)
                if (root.highlightedZoneIds && root.highlightedZoneIds.length > 0) {
                    var zoneId = modelData.zoneId || modelData.id || "";
                    if (zoneId !== "") {
                        for (var i = 0; i < root.highlightedZoneIds.length; i++) {
                            if (String(root.highlightedZoneIds[i]) === String(zoneId))
                                return true;
                        }
                    }
                }
                return false;
            }
            // Track per-zone hover state
            property bool isZoneHovered: root.interactive && zoneMouseArea.containsMouse
            /// Whether this zone renders in the highlighted state. The card-level
            /// states only apply when no specific zone is singled out — see
            /// `root.hasZoneSelection`.
            readonly property bool isZoneHighlighted: isZoneSelected || isZoneHovered || (!root.hasZoneSelection && (root.isActive || root.isHovered))
            // Detect screen boundaries (tolerance 0.01)
            readonly property real edgeTolerance: 0.01
            readonly property real leftGap: relX < edgeTolerance ? root.edgeGap : root.zonePadding / 2
            readonly property real topGap: relY < edgeTolerance ? root.edgeGap : root.zonePadding / 2
            readonly property real rightGap: (relX + relWidth) > (1 - edgeTolerance) ? root.edgeGap : root.zonePadding / 2
            readonly property real bottomGap: (relY + relHeight) > (1 - edgeTolerance) ? root.edgeGap : root.zonePadding / 2

            // Position and size — overlapping layouts skip edge gaps/padding so the
            // algorithm's raw geometry is rendered as-is.
            x: root.producesOverlappingZones ? (relX * root.width) : (relX * root.width + leftGap)
            y: root.producesOverlappingZones ? (relY * root.height) : (relY * root.height + topGap)
            width: root.producesOverlappingZones ? Math.max(root.minZoneSize, relWidth * root.width) : Math.max(root.minZoneSize, relWidth * root.width - leftGap - rightGap)
            height: root.producesOverlappingZones ? Math.max(root.minZoneSize, relHeight * root.height) : Math.max(root.minZoneSize, relHeight * root.height - topGap - bottomGap)
            // Scale on hover (only if hoverScale > 1.0)
            scale: isZoneHovered && root.hoverScale > 1 ? root.hoverScale : 1
            z: isZoneHovered ? 10 : 1
            transformOrigin: Item.Center
            // Zone fill color - use highlight color when selected/hovered, inactive color otherwise.
            // Discard the colour's own alpha so `opacity` is the SOLE alpha control;
            // otherwise the two multiply (colour.a × opacity) and the zone previews
            // far more transparent than the configured opacity — mismatching the live
            // shader overlay (overlay_data.cpp sets FillA = activeOpacity).
            // The plain border below is the exception: it keeps its colour's
            // carried alpha deliberately (pipeline border alpha ≈0.78 matches
            // the live overlay), while the fill and hover border strip.
            color: {
                var base = isZoneHighlighted ? root.highlightColor : root.inactiveColor;
                return Qt.rgba(base.r, base.g, base.b, 1.0);
            }
            opacity: isZoneHighlighted ? root.activeOpacity : root.inactiveOpacity
            border.color: {
                // Brighter border when hovered
                if (isZoneHovered)
                    return Qt.alpha(Qt.lighter(root.highlightColor, 1.2), 1.0);

                return root.borderColor;
            }
            border.width: (isZoneSelected || isZoneHovered) ? 2 : 1
            radius: Kirigami.Units.smallSpacing * 0.5

            // Zone number label
            Label {
                anchors.centerIn: parent
                // Use actual zoneNumber from data if available, otherwise fall back to index + 1
                text: modelData.zoneNumber !== undefined ? modelData.zoneNumber : (index + 1)
                font.pixelSize: Math.min(parent.width, parent.height) * 0.4 * root.fontSizeScale
                font.weight: root.fontWeight
                font.italic: root.fontItalic
                font.underline: root.fontUnderline
                font.strikeout: root.fontStrikeout
                font.family: root.fontFamily
                color: root.labelFontColor
                opacity: zoneRect.isZoneHighlighted ? 0.9 : 0.6
                visible: {
                    if (!root.showZoneNumbers)
                        return false;

                    if (parent.width < 16 || parent.height < 16)
                        return false;

                    var display = root.zoneNumberDisplay;
                    switch (display) {
                    case "none":
                        return false;
                    case "first":
                        return index === 0;
                    case "last":
                        return index === root.zones.length - 1;
                    case "firstAndLast":
                        return index === 0 || index === root.zones.length - 1;
                    default:
                        return true; // "all"
                    }
                }

                Behavior on opacity {
                    PhosphorMotionAnimation {
                        // Direction is taken from the same predicate driving
                        // the label's `opacity` binding above (active/hover/
                        // select state). Reading `opacity` itself would never
                        // pick the fadeOut leg — the binding only moves
                        // between 0.6 and 0.9, both of which are > 0.5.
                        profile: zoneRect.isZoneHighlighted ? "widget.fadeIn" : "widget.fadeOut"
                        durationOverride: root.animationDuration
                    }
                }
            }

            // Mouse interaction for zone selection (only when interactive)
            MouseArea {
                id: zoneMouseArea

                anchors.fill: parent
                anchors.margins: -Math.round(Kirigami.Units.smallSpacing / 2) // Slightly larger hit area
                hoverEnabled: root.interactive && root.visible
                enabled: root.interactive
                onEntered: root.zoneHovered(index)
            }

            // Animations — durationOverride binds to root.animationDuration
            // so consumer Items that override the default 150 ms (LayoutCard,
            // AlgorithmPreview) still drive the timing here.
            Behavior on color {
                PhosphorMotionAnimation {
                    profile: "widget.zoneHighlight"
                    durationOverride: root.animationDuration
                }
            }

            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: "widget.zoneHighlight"
                    durationOverride: root.animationDuration
                }
            }

            Behavior on scale {
                // OutBack overshoot=1.20 feel — restored faithfully via the
                // osd-pop curve referenced through widget.zoneHighlight.pop.
                PhosphorMotionAnimation {
                    profile: "widget.zoneHighlight.pop"
                    durationOverride: root.animationDuration
                }
            }

            // Border feedback uses the half-duration widget.zoneHighlight.border
            // profile so the border snaps in twice as fast as the fill —
            // matches the pre-PR-344 `duration: animationDuration / 2` shape.
            Behavior on border.color {
                PhosphorMotionAnimation {
                    profile: "widget.zoneHighlight.border"
                    durationOverride: Math.round(root.animationDuration / 2)
                }
            }

            Behavior on border.width {
                PhosphorMotionAnimation {
                    profile: "widget.zoneHighlight.border"
                    durationOverride: Math.round(root.animationDuration / 2)
                }
            }
        }
    }

    // Master indicator dots overlaid on master zone(s) for autotile algorithms
    Repeater {
        model: root.showMasterDot ? (root.zones || []) : []

        Rectangle {
            required property var modelData
            required property int index
            // Mirror the zone rect's geometry handling above: [0,1]-clamped
            // relative coordinates, and gap offsets skipped for overlapping
            // layouts so the dot tracks the zone's actual rendered origin.
            readonly property var relGeo: modelData.relativeGeometry || ({})
            readonly property real relX: Math.max(0, Math.min((modelData.x !== undefined ? modelData.x : (relGeo.x || 0)), 1))
            readonly property real relY: Math.max(0, Math.min((modelData.y !== undefined ? modelData.y : (relGeo.y || 0)), 1))
            readonly property real leftOffset: root.producesOverlappingZones ? 0 : (relX < 0.01 ? root.edgeGap : root.zonePadding / 2)
            readonly property real topOffset: root.producesOverlappingZones ? 0 : (relY < 0.01 ? root.edgeGap : root.zonePadding / 2)

            visible: index < root.masterCount
            Accessible.ignored: true
            x: relX * root.width + leftOffset + Kirigami.Units.smallSpacing
            y: relY * root.height + topOffset + Kirigami.Units.smallSpacing
            width: Kirigami.Units.smallSpacing * 2
            height: Kirigami.Units.smallSpacing * 2
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.highlightColor
        }
    }
}
