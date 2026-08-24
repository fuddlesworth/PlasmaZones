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

    /// Array of zone objects with relativeGeometry: { x, y, width, height }.
    /// Must be a JS array or a QVariantList, not an arbitrary model: the tab
    /// band filters it, so a type without `filter` breaks that binding.
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
    /// Which way the scrolling strip runs, drawn as a chevron on each of the
    /// two edges the strip continues past: "none" (the default, every layout
    /// host), "horizontal" or "vertical".
    ///
    /// This is the ONLY thing a strip preview says about continuation. The
    /// sketch it replaced drew three invented columns with their outer two
    /// clipped at the box edges, which read as real windows because the
    /// renderer draws real zones the same way. A chevron cannot be mistaken
    /// for a window, and unlike the sketch it is honest on a populated strip
    /// too, so every scrolling surface carries it rather than only the empty
    /// one.
    property string stripAxisHint: "none"
    /// Tick colour. Theme text by default, deliberately NOT the zone palette:
    /// the ticks describe the strip, not a zone, and a user who has recoloured
    /// their zones should not lose them into the fill.
    property color stripAxisHintColor: Kirigami.Theme.textColor
    /// Whether to show master indicator dots on master zone(s)
    property bool showMasterDot: false
    /// Number of master zones to mark with indicator dots
    property int masterCount: 1

    readonly property bool stripAxisHintVertical: root.stripAxisHint === "vertical"
    readonly property bool stripAxisHintVisible: root.stripAxisHintVertical || root.stripAxisHint === "horizontal"
    /// Arm length of one chevron stroke. Scaled off the box's SHORT side so a
    /// tick keeps its proportions on a portrait screen's preview, floored so
    /// it survives the smallest host (the layout combo's thumbnail) and capped
    /// so it does not grow into a wedge on the Monitors page's large box.
    readonly property real stripAxisHintArm: Math.max(3, Math.min(9, Math.min(root.width, root.height) * 0.085))
    /// Stroke thickness, floored at 1 for the same reason the tab band floors
    /// its own: below a pixel the stroke stops being drawn at all.
    readonly property real stripAxisHintThickness: Math.max(1, root.stripAxisHintArm * 0.22)

    /// Side of one chevron's box. SQUARE on purpose: `rotation` pivots on the
    /// item's centre, so a square is the only box whose on-screen extent is
    /// unchanged by the 90 and 270 degree legs. With a snug arm*cos45 by
    /// arm*2*sin45 box the vertical ticks would need their own inset algebra
    /// to undo the swap, and that algebra is exactly the kind that goes wrong
    /// silently on one axis only.
    readonly property real stripAxisHintSide: root.stripAxisHintArm * Math.SQRT2

    /// One chevron, built pointing LEFT and rotated into the other three
    /// directions. The two strokes meet at the tip and splay by 45 degrees,
    /// so the arm length is the hypotenuse of each stroke.
    component AxisChevron: Item {
        id: chevron

        /// 0 left, 1 right, 2 up, 3 down.
        required property int direction
        readonly property real arm: root.stripAxisHintArm
        readonly property real thickness: root.stripAxisHintThickness
        width: root.stripAxisHintSide
        height: root.stripAxisHintSide
        rotation: {
            switch (direction) {
            case 1:
                return 180;
            case 2:
                return 90;
            case 3:
                return 270;
            default:
                return 0;
            }
        }
        opacity: 0.5
        Accessible.ignored: true

        Repeater {
            // Two strokes, splayed either side of the tip. The model IS the
            // rotation, so the pair cannot drift out of symmetry.
            model: [-45, 45]

            Rectangle {
                required property real modelData

                width: chevron.arm
                height: chevron.thickness
                radius: chevron.thickness / 2
                color: root.stripAxisHintColor
                // The tip, centred in the square box: the shape spans
                // arm*cos45 across, so half the slack sits either side.
                x: (chevron.width - chevron.arm * Math.SQRT1_2) / 2
                y: chevron.height / 2 - chevron.thickness / 2
                // Pivot on the tip, not the stroke's centre: both strokes must
                // share one origin or the chevron opens into a Z.
                transformOrigin: Item.Left
                rotation: modelData
            }
        }
    }

    // The axis ticks: one chevron on each edge the strip continues past.
    // z 1 rather than declaration order, because an edge column of a live
    // strip lands exactly here and a tick under its fill is a smudge. Both
    // are non-interactive: ZonePreview carries no MouseAreas (the selector
    // hit-tests the delegates from C++) and these must not introduce one.
    AxisChevron {
        objectName: "zonePreviewAxisTickStart"
        z: 1
        direction: root.stripAxisHintVertical ? 2 : 0
        visible: root.stripAxisHintVisible
        x: root.stripAxisHintVertical ? (root.width - width) / 2 : root.edgeGap
        y: root.stripAxisHintVertical ? root.edgeGap : (root.height - height) / 2
    }

    AxisChevron {
        objectName: "zonePreviewAxisTickEnd"
        z: 1
        direction: root.stripAxisHintVertical ? 3 : 1
        visible: root.stripAxisHintVisible
        x: root.stripAxisHintVertical ? (root.width - width) / 2 : root.width - root.edgeGap - width
        y: root.stripAxisHintVertical ? root.height - root.edgeGap - height : (root.height - height) / 2
    }

    /// Where a zone's tile lands, in one place. Three overlays need it — the
    /// zone rect itself, the master dots and the scrolling tab bands — and
    /// each one drifting off its own copy is how an overlay ends up labelling
    /// a tile it no longer sits on.
    ///
    /// Zones may come from LayoutPreview (flat x/y/w/h) or the legacy
    /// zonesToVariantList shape (nested relativeGeometry); flat wins, nested
    /// is the fallback. Relative coordinates are clamped to [0, 1] because a
    /// fixed-geometry layout whose reference screen differs from the current
    /// one can exceed 1.0. Overlapping layouts skip the edge gaps and padding
    /// so the algorithm's raw geometry renders as-is.
    component TileGeometry: QtObject {
        required property var zone

        // Detect screen boundaries (tolerance 0.01)
        readonly property real edgeTolerance: 0.01
        readonly property var relGeo: zone.relativeGeometry || ({})
        readonly property real relX: Math.max(0, Math.min((zone.x !== undefined ? zone.x : (relGeo.x || 0)), 1))
        readonly property real relY: Math.max(0, Math.min((zone.y !== undefined ? zone.y : (relGeo.y || 0)), 1))
        readonly property real relWidth: Math.max(0, Math.min((zone.width !== undefined ? zone.width : (relGeo.width || 0.25)), 1 - relX))
        readonly property real relHeight: Math.max(0, Math.min((zone.height !== undefined ? zone.height : (relGeo.height || 1)), 1 - relY))
        readonly property real leftGap: root.producesOverlappingZones ? 0 : (relX < edgeTolerance ? root.edgeGap : root.zonePadding / 2)
        readonly property real topGap: root.producesOverlappingZones ? 0 : (relY < edgeTolerance ? root.edgeGap : root.zonePadding / 2)
        readonly property real rightGap: root.producesOverlappingZones ? 0 : ((relX + relWidth) > (1 - edgeTolerance) ? root.edgeGap : root.zonePadding / 2)
        readonly property real bottomGap: root.producesOverlappingZones ? 0 : ((relY + relHeight) > (1 - edgeTolerance) ? root.edgeGap : root.zonePadding / 2)
        readonly property real tileX: relX * root.width + leftGap
        readonly property real tileY: relY * root.height + topGap
        readonly property real tileWidth: Math.max(root.minZoneSize, relWidth * root.width - leftGap - rightGap)
        readonly property real tileHeight: Math.max(root.minZoneSize, relHeight * root.height - topGap - bottomGap)
    }

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

            TileGeometry {
                id: geometry

                zone: zoneRect.modelData
            }
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
            x: geometry.tileX
            y: geometry.tileY
            width: geometry.tileWidth
            height: geometry.tileHeight
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
            id: masterDot

            required property var modelData
            required property int index

            TileGeometry {
                id: dotGeometry

                zone: masterDot.modelData
            }

            visible: index < root.masterCount
            Accessible.ignored: true
            // Inset from the tile's own origin so the dot sits inside the zone
            // it marks rather than on its corner.
            x: dotGeometry.tileX + Kirigami.Units.smallSpacing
            y: dotGeometry.tileY + Kirigami.Units.smallSpacing
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
    // and how far it reaches match the screen, with one caveat: the proportion
    // is measured against the column's TRUE extent (engine_query.cpp), while
    // the rect it ships beside is clipped to the work area. On a column clipped
    // along the indicator's own long axis the band therefore reads shorter than
    // the bar on screen. That is inherited from the compositor's own accepted
    // limit (engine_apply.cpp, "KNOWN LIMIT"), which keeps the bar on the true
    // extent and explicitly rejected re-deriving it from the clamped one.
    //
    // Carrying tab data is the whole gate: a layout zone has no tab keys, so
    // the filter below leaves layout hosts with no band items at all rather
    // than one inert item per zone.
    Repeater {
        model: (root.zones || []).filter(zone => (zone.tabCount || 0) > 0)

        Item {
            id: tabBand

            // Read by test_zone_preview_highlight to find the bands among the
            // preview's children, the same way the zone delegate's objectName
            // is read by the selector's hit-test.
            objectName: "zonePreviewTabIndicator"

            required property var modelData
            // PhosphorScrollEngine::TabIndicatorPosition, named rather than
            // spelled as bare numbers so a renumbering on the C++ side is
            // greppable here instead of silently rotating the band.
            readonly property int positionLeft: 0
            readonly property int positionRight: 1
            readonly property int positionTop: 2
            // Absent key, 0, or a malformed value all mean "this column draws
            // no indicator" — the single gate, matching the null indicator
            // rect that gates the compositor's own tab payload. The model
            // above filters on it, so a band only exists where it is positive.
            readonly property int tabCount: modelData.tabCount || 0
            // Clamped, not defaulted: an index outside the pill row would
            // leave the band with nothing lit, which reads as "no tab is
            // current" on a column that always has one.
            readonly property int activeTab: Math.max(0, Math.min(modelData.activeTab || 0, tabCount - 1))
            readonly property int tabPosition: modelData.tabPosition || 0
            readonly property bool vertical: tabPosition === positionLeft || tabPosition === positionRight
            readonly property real lengthProportion: Math.max(0, Math.min(modelData.tabLength !== undefined ? modelData.tabLength : 1, 1))

            // The band has to track the tile as DRAWN, gaps and minimum size
            // included, or it drifts off the tile it labels.
            TileGeometry {
                id: bandGeometry

                zone: tabBand.modelData
            }

            // Named locally because the thickness, length and placement
            // expressions below read them repeatedly; they are nothing but
            // bandGeometry under a shorter name.
            readonly property real tileX: bandGeometry.tileX
            readonly property real tileY: bandGeometry.tileY
            readonly property real tileWidth: bandGeometry.tileWidth
            readonly property real tileHeight: bandGeometry.tileHeight

            // Floored rather than scaled: below about two pixels the band
            // stops reading as a band at thumbnail scale. Half of smallSpacing
            // is the target above that floor, capped at a quarter of the
            // tile's short side so a clipped edge column of the strip, which
            // can be a handful of pixels wide, is not buried under its own
            // indicator. At the shipping smallSpacing of 4 the floor and the
            // target coincide and the cap cannot bind; it earns its keep on a
            // theme with a larger spacing unit.
            readonly property real minThickness: 2
            readonly property real thickness: Math.max(minThickness, Math.min(Kirigami.Units.smallSpacing / 2, Math.min(tileWidth, tileHeight) / 4))
            // Same floor, and the same reason: a band shorter than this reads
            // as a speck rather than as an indicator.
            readonly property real bandLength: Math.max(minThickness, (vertical ? tileHeight : tileWidth) * lengthProportion)
            // One pixel between pills, dropped once the pills themselves are
            // down to about that: below it the gaps eat the indicator and a
            // five-tab column reads as an empty band. Three pixels is the
            // width at which a one-pixel gap still leaves a pill wider than
            // the gap beside it.
            readonly property real minPillLengthForSpacing: 3
            readonly property real pillSpacing: (bandLength / Math.max(1, tabCount)) > minPillLengthForSpacing ? 1 : 0
            readonly property real pillLength: Math.max(1, (bandLength - pillSpacing * (tabCount - 1)) / Math.max(1, tabCount))

            // Both pill extents are floored, so a column with more tabs than
            // the band has pixels lays out a row longer than the band. Clip
            // rather than let it run past, since outside the band is the
            // neighbouring tile at this scale.
            clip: true
            Accessible.ignored: true

            // Centered on the long axis, matching indicatorRectFor, and flush
            // with the tile's edge on the short one.
            x: vertical ? (tabPosition === positionLeft ? tileX : tileX + tileWidth - thickness) : tileX + (tileWidth - bandLength) / 2
            y: vertical ? tileY + (tileHeight - bandLength) / 2 : (tabPosition === positionTop ? tileY : tileY + tileHeight - thickness)
            width: vertical ? thickness : bandLength
            height: vertical ? bandLength : thickness

            Repeater {
                // The band's own existence is the gate, so this does not read
                // the band's visibility: Item.visible is the EFFECTIVE state,
                // and an ancestor hiding the preview would tear every pill
                // down and rebuild it on the way back.
                model: tabBand.tabCount

                Rectangle {
                    required property int index

                    x: tabBand.vertical ? 0 : index * (tabBand.pillLength + tabBand.pillSpacing)
                    y: tabBand.vertical ? index * (tabBand.pillLength + tabBand.pillSpacing) : 0
                    width: tabBand.vertical ? tabBand.thickness : tabBand.pillLength
                    height: tabBand.vertical ? tabBand.pillLength : tabBand.thickness
                    radius: tabBand.thickness / 2
                    Accessible.ignored: true
                    // The indicator's OWN fallbacks, not the zone palette:
                    // ZoneColorDefaults owns these and the compositor's
                    // resolveTabColor mirrors them, so a user who has recoloured
                    // their zones still sees pills that match the bar on screen.
                    // A configured tab colour is not reachable without new wire
                    // keys, so the default configuration is what agrees here.
                    color: index === tabBand.activeTab ? ZoneColorDefaults.tabActiveColor : ZoneColorDefaults.tabInactiveBarColor
                }
            }
        }
    }
}
