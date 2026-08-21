// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Shared zone preview component for rendering layout zones
 *
 * Instantiated directly and via LayoutCard across settings and overlay surfaces.
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
    /// Array of zone IDs to highlight (for navigation OSD zone highlighting)
    property var highlightedZoneIds: []
    /// Whether specific zones are singled out, by index or by zone ID. The
    /// card-level `isActive` / `isHovered` states must not light every zone
    /// while this holds, or the singled-out zone renders identically to its
    /// siblings and the selection is invisible. Consumers that pass no per-zone
    /// selection (layout picker, OSD, settings thumbnails) keep the whole-card
    /// highlight.
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
    /// Whether a zone carrying scrolling tab data draws its column's tab
    /// indicator. Inert for every host that renders a LAYOUT: layout zones
    /// carry no tab keys, so the model below comes out empty for them. The
    /// two hosts that do feed tab data are the strip previews — the Monitors
    /// page thumbnail (over visibleStripJson) and the daemon's OSD strip card
    /// (in-process, same keys) — and a tabbed column is indistinguishable from
    /// a plain one without it, since the strip walk lists only the shown tab.
    property bool showTabIndicators: true

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
            // Check if this zone is selected (by index or by zone ID)
            property bool isZoneSelected: {
                // Option 1: Highlight by index (layout selector mode)
                if (root.selectedZoneIndex === index)
                    return true;

                // Option 2: Highlight by zone ID (navigation OSD mode)
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
            /// Whether this zone renders in the highlighted state. The card-level
            /// states only apply when no specific zone is singled out — see
            /// `root.hasZoneSelection`.
            readonly property bool isZoneHighlighted: isZoneSelected || (!root.hasZoneSelection && (root.isActive || root.isHovered))
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
            // Zone fill color - use highlight color when selected/hovered, inactive color otherwise.
            // The configured fill opacity is baked into the FILL colour's alpha
            // (discarding the colour's own carried alpha so the two don't
            // multiply), matching the live shader overlay where only FillA
            // carries the opacity (overlay_data.cpp sets FillA = activeOpacity
            // and leaves border alpha untouched). Delegate `opacity` must stay
            // 1 — it would multiply into the border AND the zone-number label,
            // dimming both far below the pipeline values.
            // The plain border below keeps its colour's carried alpha
            // deliberately (pipeline border alpha ≈0.78 matches the live
            // overlay), while the fill strips its own.
            color: {
                var base = isZoneHighlighted ? root.highlightColor : root.inactiveColor;
                return Qt.rgba(base.r, base.g, base.b, isZoneHighlighted ? root.activeOpacity : root.inactiveOpacity);
            }
            border.color: root.borderColor
            border.width: isZoneSelected ? 2 : 1
            radius: Kirigami.Units.smallSpacing * 0.5

            // Zone number label
            Label {
                anchors.centerIn: parent
                // Use actual zoneNumber from data if available, otherwise fall back to index + 1
                text: modelData.zoneNumber !== undefined ? modelData.zoneNumber : (index + 1)
                // Clamped against the zone box: labelFontSizeScale reaches
                // 3.0, and unclamped the glyph box would be 1.2x the zone's
                // shorter side — the numbers spill over neighbouring zones,
                // worst in fixed-size hosts (the OSD's preview does not grow
                // with the font scale). 0.6 keeps the digit inside the box
                // with breathing room at every scale.
                font.pixelSize: Math.min(Math.min(parent.width, parent.height) * 0.4 * root.fontSizeScale, Math.min(parent.width, parent.height) * 0.6)
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

                    // 16px legibility floor. Coupled by value to the hosts
                    // that must keep every zone number visible — the
                    // scrolling strip preview raises LayoutThumbnail's
                    // minZoneSize to this same 16 (MonitorStatePage.qml) —
                    // so a change here must move those hosts with it.
                    if (parent.width < 16 || parent.height < 16)
                        return false;

                    var display = root.zoneNumberDisplay;
                    switch (display) {
                    case "none":
                        return false;
                    case "first":
                        return index === 0;
                    case "last":
                        return index === (root.zones || []).length - 1;
                    case "firstAndLast":
                        return index === 0 || index === (root.zones || []).length - 1;
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

            // Animations — durationOverride binds to root.animationDuration
            // so consumer Items that override the default 150 ms (LayoutCard,
            // AlgorithmPreview) still drive the timing here.
            Behavior on color {
                PhosphorMotionAnimation {
                    profile: "widget.zoneHighlight"
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
            color: Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, 1)
        }
    }

    // Tab indicators for the scrolling strip previews: one pill per tab of a
    // tabbed column, on the edge the real indicator runs along, with the shown
    // tab lit. The strip walk lists only a column's SHOWN tab, so without this
    // a column holding five tabbed windows draws exactly like a column holding
    // one, and the preview quietly under-reports the strip.
    //
    // Two deliberate departures from the indicator on screen, both because
    // this is a thumbnail of a whole output:
    //   * THICKNESS is floored rather than scaled. The configured width is a
    //     handful of pixels, which is a fraction of one here, so a faithful
    //     scale draws nothing at all. The payload carries no thickness for
    //     that reason (PhosphorProtocol StripPreviewKey).
    //   * The band is drawn INSIDE the tile's edge whatever placeWithinColumn
    //     says. Outside the column it would land on the neighbouring tile at
    //     this scale and read as that tile's indicator.
    // Position and length ARE the resolved ones, so which edge it runs along
    // and how far it reaches match the screen.
    Repeater {
        model: root.showTabIndicators ? (root.zones || []) : []

        Item {
            id: tabBand

            // Read by test_zone_preview_highlight to find the bands among the
            // preview's children, the same way the zone delegate's objectName
            // is read by the selector's hit-test.
            objectName: "zonePreviewTabIndicator"

            required property var modelData
            // Absent key, 0, or a malformed value all mean "this column draws
            // no indicator" — the single gate, matching the null indicator
            // rect that gates the compositor's own tab payload.
            readonly property int tabCount: modelData.tabCount || 0
            readonly property int activeTab: modelData.activeTab || 0
            // 0 Left, 1 Right, 2 Top, 3 Bottom (TabIndicatorPosition).
            readonly property int tabPosition: modelData.tabPosition || 0
            readonly property bool vertical: tabPosition === 0 || tabPosition === 1
            readonly property real lengthProportion: Math.max(0, Math.min(modelData.tabLength !== undefined ? modelData.tabLength : 1, 1))

            // Mirror of the zone delegate's geometry, the same way the master
            // dots mirror it: the band has to track the tile as DRAWN, gaps
            // and minimum size included, or it drifts off the tile it labels.
            readonly property var relGeo: modelData.relativeGeometry || ({})
            readonly property real relX: Math.max(0, Math.min((modelData.x !== undefined ? modelData.x : (relGeo.x || 0)), 1))
            readonly property real relY: Math.max(0, Math.min((modelData.y !== undefined ? modelData.y : (relGeo.y || 0)), 1))
            readonly property real relWidth: Math.max(0, Math.min((modelData.width !== undefined ? modelData.width : (relGeo.width || 0.25)), 1 - relX))
            readonly property real relHeight: Math.max(0, Math.min((modelData.height !== undefined ? modelData.height : (relGeo.height || 1)), 1 - relY))
            readonly property real leftGap: root.producesOverlappingZones ? 0 : (relX < 0.01 ? root.edgeGap : root.zonePadding / 2)
            readonly property real topGap: root.producesOverlappingZones ? 0 : (relY < 0.01 ? root.edgeGap : root.zonePadding / 2)
            readonly property real rightGap: root.producesOverlappingZones ? 0 : ((relX + relWidth) > 0.99 ? root.edgeGap : root.zonePadding / 2)
            readonly property real bottomGap: root.producesOverlappingZones ? 0 : ((relY + relHeight) > 0.99 ? root.edgeGap : root.zonePadding / 2)
            readonly property real tileX: relX * root.width + leftGap
            readonly property real tileY: relY * root.height + topGap
            readonly property real tileWidth: Math.max(root.minZoneSize, relWidth * root.width - leftGap - rightGap)
            readonly property real tileHeight: Math.max(root.minZoneSize, relHeight * root.height - topGap - bottomGap)

            // Thick enough to see, and never more than a quarter of the tile's
            // short side — a clipped edge column of the strip can be a few
            // pixels wide, and a fixed thickness would bury it.
            readonly property real thickness: Math.max(2, Math.min(Kirigami.Units.smallSpacing / 2, Math.min(tileWidth, tileHeight) / 4))
            readonly property real bandLength: Math.max(2, (vertical ? tileHeight : tileWidth) * lengthProportion)
            // One pixel between pills, dropped once the pills themselves are
            // down to about that: below it the gaps eat the indicator and a
            // five-tab column reads as an empty band.
            readonly property real pillSpacing: (bandLength / Math.max(1, tabCount)) > 3 ? 1 : 0
            readonly property real pillLength: Math.max(1, (bandLength - pillSpacing * (tabCount - 1)) / Math.max(1, tabCount))

            visible: tabCount > 0
            Accessible.ignored: true

            // Centered on the long axis, matching indicatorRectFor, and flush
            // with the tile's edge on the short one.
            x: vertical ? (tabPosition === 0 ? tileX : tileX + tileWidth - thickness) : tileX + (tileWidth - bandLength) / 2
            y: vertical ? tileY + (tileHeight - bandLength) / 2 : (tabPosition === 2 ? tileY : tileY + tileHeight - thickness)
            width: vertical ? thickness : bandLength
            height: vertical ? bandLength : thickness

            Repeater {
                model: tabBand.visible ? tabBand.tabCount : 0

                Rectangle {
                    required property int index

                    x: tabBand.vertical ? 0 : index * (tabBand.pillLength + tabBand.pillSpacing)
                    y: tabBand.vertical ? index * (tabBand.pillLength + tabBand.pillSpacing) : 0
                    width: tabBand.vertical ? tabBand.thickness : tabBand.pillLength
                    height: tabBand.vertical ? tabBand.pillLength : tabBand.thickness
                    radius: tabBand.thickness / 2
                    // Theme roles, like every other colour here. The real
                    // indicator's own colours are configurable and can follow
                    // the theme themselves, so a preview that read them would
                    // have to carry three more keys to end up in the same
                    // place for the default configuration.
                    color: index === tabBand.activeTab ? Qt.rgba(root.highlightColor.r, root.highlightColor.g, root.highlightColor.b, 1) : Qt.rgba(root.borderColor.r, root.borderColor.g, root.borderColor.b, 0.7)
                }
            }
        }
    }
}
