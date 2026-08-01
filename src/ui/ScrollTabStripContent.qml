// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Tab indicators for tabbed scrolling columns.
 *
 * Display-only and click-through: one indicator per tabbed column, drawn in
 * the rect the scrolling engine resolved for it. The model arrives from C++ as
 * `strips` — a list of maps with x / y / width / height (shell-window
 * coordinates), `position` (0 left, 1 right, 2 top, 3 bottom) and `tabs` (list
 * of {title, active, urgent}). Updates are plain property writes; the component
 * is not re-instantiated per relayout.
 *
 * TWO STYLES, chosen by `style`:
 *
 *  - Chips (0): a pill listing the tabs by title, the indicator PlasmaZones
 *    shipped before the settings family existed. It SELF-SIZES on its short
 *    axis, so the engine's `width` acts as a floor rather than a ceiling and
 *    the chips stay legible at any thickness. On its long axis it is clamped
 *    to the resolved rect and scrolls just far enough to keep the ACTIVE chip
 *    inside, clipping whichever chips fall outside with no "+N" affordance:
 *    the highlight is what the indicator exists to show, so it is the one chip
 *    that always stays visible.
 *
 *  - Bar (1): niri's thin run of coloured segments, one per tab, filling the
 *    resolved rect exactly. No titles, so it costs no room and reads at a
 *    glance. Segments split the long axis evenly.
 *
 * Both honour the resolved rect's ORIENTATION: a left/right indicator stacks
 * its tabs down the column, a top/bottom one lays them across.
 *
 * The strip carries no accessible surface because it is click-through by
 * design.
 */

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// NOTE: no `shaderAnchor` on this content root, unlike the PopupFrame
// cards — a multi-indicator strip has no single card rect to anchor a surface
// shader to, and no applyDecoration call targets the ScrollTabs role. The
// animator's bare-slot fallback (no capture, no sibling hiding) is the
// intended presentation here; if a pack ever targets this role, revisit.
Item {
    id: root

    /// Strip entries pushed by the daemon (see file doc).
    property var strips: []

    /// A tab was clicked; @p windowId is the canonical id the daemon put in
    /// the payload. The daemon focuses that window, which makes it the shown
    /// tab through the normal focus path rather than by poking the strip.
    ///
    /// Clicks only reach here because the daemon hands the shell an input
    /// region covering the indicator rects. Everywhere else the surface is
    /// click-through, so the MouseAreas below can be plain and unconditional.
    signal tabActivated(string windowId)

    // ── Paint settings, pushed by the daemon from Scrolling.TabIndicator.
    // The engine never sees these (see IScrollSettings): they cannot change a
    // resolved rect, so they travel straight from config to here.

    /// 0 = title chips, 1 = segment bar.
    property int style: 0
    /// Gap between individual tabs, in logical pixels.
    property int gapsBetweenTabs: 0
    /// Per-tab corner radius; NEGATIVE means fully rounded (half the tab's
    /// short extent), which is how the chips pill has always looked.
    property int cornerRadius: -1
    /// Tab colours. EMPTY means "follow the theme" — see the resolved
    /// properties below for what each falls back to.
    property string activeColor: ""
    property string inactiveColor: ""
    property string urgentColor: ""

    // User overlay font, pushed by the daemon (writeFontProperties) like
    // every other overlay slot; falls back to the theme's small font.
    property string fontFamily: ""
    property real fontSizeScale: 1.0
    property int fontWeight: Font.Normal
    property bool fontItalic: false
    property bool fontStrikeout: false
    property bool fontUnderline: false

    // Theme fallbacks, niri's third resolution tier ("the colour matching the
    // window border or focus ring") spelled in Plasma's vocabulary. Urgent
    // falls back to the negative role rather than a hardcoded red so it tracks
    // the user's colour scheme, including high-contrast ones.
    readonly property color resolvedActiveColor: root.activeColor.length > 0 ? root.activeColor : Kirigami.Theme.highlightColor
    readonly property color resolvedUrgentColor: root.urgentColor.length > 0 ? root.urgentColor : Kirigami.Theme.negativeTextColor
    // The inactive fallback differs per style ON PURPOSE. A chip carries its
    // title on top, so its resting state is no fill at all and the label does
    // the work; a bar segment has nothing but its fill, so an unfilled one
    // would be invisible and the indicator would under-report its tab count.
    readonly property color resolvedInactiveChipColor: root.inactiveColor.length > 0 ? root.inactiveColor : "transparent"
    readonly property color resolvedInactiveBarColor: root.inactiveColor.length > 0 ? root.inactiveColor : Qt.alpha(Kirigami.Theme.textColor, 0.35)

    /// Radius for a tab whose short extent is @p thickness, resolving the
    /// pill sentinel. Clamped so an oversized radius cannot exceed a half
    /// extent and render as a lozenge with inverted corners.
    function tabRadius(thickness) {
        if (root.cornerRadius < 0)
            return thickness / 2;
        return Math.min(root.cornerRadius, thickness / 2);
    }

    /// Colour for @p tab, resolving niri's four tiers in order: the window
    /// rule's own colour, then the context/config colour, then the theme.
    /// (The context tier is already folded into the resolved* properties by
    /// the daemon, which layers a context override over the config value
    /// before pushing.) @p forBar picks the style-specific inactive fallback.
    ///
    /// Urgency outranks the active highlight: a tab already in view needs no
    /// highlight, whereas an urgent one the user cannot see is the whole point
    /// of the colour.
    function tabColor(tab, forBar) {
        var overrides = tab.colors;
        if (tab.urgent === true)
            return overrides && overrides.urgentColor ? overrides.urgentColor : root.resolvedUrgentColor;
        if (tab.active === true)
            return overrides && overrides.activeColor ? overrides.activeColor : root.resolvedActiveColor;
        if (overrides && overrides.inactiveColor)
            return overrides.inactiveColor;
        return forBar ? root.resolvedInactiveBarColor : root.resolvedInactiveChipColor;
    }

    Repeater {
        model: root.strips

        delegate: Item {
            id: indicator

            required property var modelData

            // The rect the engine resolved. The chips style may exceed it on
            // the short axis (see the file doc); the bar style fills it.
            readonly property int slotX: modelData.x
            readonly property int slotY: modelData.y
            readonly property int slotWidth: modelData.width
            readonly property int slotHeight: modelData.height
            readonly property var tabs: modelData.tabs
            // Left (0) and right (1) run the indicator DOWN the column, so its
            // long axis is the rect's height.
            readonly property bool vertical: modelData.position === 0 || modelData.position === 1
            /// Left specifically, which decides which way a rotated title
            /// leans (see the chip label below).
            readonly property bool leftEdge: modelData.position === 0
            readonly property int longExtent: vertical ? slotHeight : slotWidth
            readonly property int tabCount: tabs ? tabs.length : 0

            x: slotX
            y: slotY
            width: slotWidth
            height: slotHeight

            // ── Segment bar ─────────────────────────────────────────────
            // Fills the resolved rect exactly: each segment takes an equal
            // share of the long axis after the inter-tab gaps are removed.
            Repeater {
                model: root.style === 1 && indicator.tabCount > 0 ? indicator.tabs : []

                delegate: Rectangle {
                    id: segment

                    required property var modelData
                    required property int index

                    // Floored at 1 so a bar too short for its tab count still
                    // paints every segment rather than silently dropping the
                    // tail to zero-width.
                    readonly property int segmentLength: Math.max(1, Math.floor((indicator.longExtent - root.gapsBetweenTabs * (indicator.tabCount - 1)) / indicator.tabCount))
                    readonly property int offset: index * (segmentLength + root.gapsBetweenTabs)

                    x: indicator.vertical ? 0 : offset
                    y: indicator.vertical ? offset : 0
                    width: indicator.vertical ? indicator.width : segmentLength
                    height: indicator.vertical ? segmentLength : indicator.height
                    color: root.tabColor(modelData, true)
                    radius: root.tabRadius(indicator.vertical ? indicator.width : indicator.height)

                    // niri's rule: with no gap between tabs the run reads as
                    // one continuous bar, so only its two ends are rounded and
                    // the interior joints stay square. With a gap every
                    // segment is its own pill. `radius` above sets all four,
                    // so these only need to SQUARE the joints.
                    readonly property bool squareLeading: root.gapsBetweenTabs === 0 && index > 0
                    readonly property bool squareTrailing: root.gapsBetweenTabs === 0 && index < indicator.tabCount - 1

                    // Top-left is the leading corner and bottom-right the
                    // trailing one in BOTH orientations, so those two need no
                    // branch; only the other diagonal swaps.
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.tabActivated(segment.modelData.windowId)
                    }

                    topLeftRadius: squareLeading ? 0 : radius
                    topRightRadius: (indicator.vertical ? squareLeading : squareTrailing) ? 0 : radius
                    bottomLeftRadius: (indicator.vertical ? squareTrailing : squareLeading) ? 0 : radius
                    bottomRightRadius: squareTrailing ? 0 : radius
                }
            }

            // ── Title-chip pill ─────────────────────────────────────────
            // FILLS the resolved rect exactly, on both axes, the way the
            // segment bar above does.
            //
            // It used to size itself to its chips and treat the rect as a
            // maximum, which quietly demoted two settings to advisory. Length
            // became a ceiling rather than a size, so raising it did nothing
            // until the titles happened to be long enough to reach it, and
            // Thickness could be overridden upward by a large overlay font.
            // Both are the engine's numbers, and `place within column`
            // reserves the column against them, so the indicator has to be
            // exactly what was reserved. Content that does not fit is the
            // clip's problem, not the rect's.
            Rectangle {
                id: pill

                visible: root.style === 0 && indicator.tabCount > 0

                anchors.fill: parent

                readonly property int shortExtent: indicator.vertical ? indicator.width : indicator.height
                // ONE inset on every side. The chips used to span the pill's
                // full thickness while sitting inset at the ends, so a chip
                // painted over the pill's border along the sides and left it
                // showing at the ends — two identical capsules, joined on one
                // axis and nested on the other. A uniform inset makes the
                // nesting deliberate, and it is what lets the radii below sit
                // concentric.
                readonly property int chipInset: Math.max(1, Math.round(Kirigami.Units.smallSpacing / 2))
                /// Thickness left for a chip once both insets are taken.
                readonly property int chipThickness: Math.max(1, shortExtent - chipInset * 2)
                /// Room along the indicator's LONG axis, and each chip's even
                /// share of it. The share is what a title elides against, so
                /// lengthening the indicator shows more of the titles instead
                /// of just adding empty space.
                ///
                /// Floored at 1, NOT at some legible minimum. A floor big
                /// enough to keep a title readable would let the run overflow
                /// the pill once there were enough tabs, and an overflowing
                /// run is clipped — those tabs would be undrawn AND
                /// unclickable, which is worse than thin. Letting the share
                /// shrink instead degrades the chips into what the segment bar
                /// already is: a sliver per tab, all of them present and
                /// hittable. At that density the bar style is the better
                /// choice anyway, and this makes the strip say so.
                readonly property int longExtent: indicator.vertical ? height : width
                readonly property int chipLongBudget: Math.max(1, Math.floor((longExtent - chipInset * 2 - root.gapsBetweenTabs * Math.max(0, indicator.tabCount - 1)) / Math.max(1, indicator.tabCount)))

                radius: root.tabRadius(shortExtent)
                color: Qt.alpha(Kirigami.Theme.backgroundColor, 0.85)
                border.width: 1 // deliberate 1px hairline; Kirigami.Units has no hairline token
                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.2)
                clip: true

                // One layout for both orientations: a Column when the
                // indicator runs down the column, a Row when it runs across.
                // Grid with a single row or column expresses that without
                // duplicating the chip delegate.
                Grid {
                    id: tabFlow

                    // Anchored at the inset, NOT centred or scrolled. The
                    // chips divide the indicator evenly (see chipLongBudget),
                    // so the run always fills the pill exactly and there is
                    // never anything to centre or scroll into view.
                    //
                    // The old content-sized run needed both, and needing them
                    // was the symptom of a real bug: the daemon hands the
                    // compositor the whole indicator rect as its input region,
                    // but only the chips carried click handlers, so every
                    // click in the leftover space was SWALLOWED by the overlay
                    // and did nothing. Dividing evenly makes the region and
                    // the tabs the same shape, so every point that takes a
                    // click belongs to exactly one tab.
                    x: pill.chipInset
                    y: pill.chipInset

                    // columns alone decides the flow (Grid ignores `rows` once
                    // columns is set): one column stacks the chips, tabCount
                    // columns lays them out in a single row.
                    columns: indicator.vertical ? 1 : Math.max(1, indicator.tabCount)
                    // Exact, including 0. Chips touching at 0 read fine
                    // because an inactive chip has no fill by default, so the
                    // run is one highlighted tab against empty space rather
                    // than a row of abutting blocks.
                    spacing: root.gapsBetweenTabs

                    Repeater {
                        model: root.style === 0 && indicator.tabCount > 0 ? indicator.tabs : []

                        delegate: Rectangle {
                            id: chip

                            required property var modelData

                            readonly property bool isActive: modelData.active === true
                            readonly property bool isUrgent: modelData.urgent === true

                            // The chip's two dimensions named by AXIS rather
                            // than by width/height, because a vertical
                            // indicator swaps them: the title runs along the
                            // indicator's long axis either way.
                            //
                            // ALONG the axis every chip takes an equal share of
                            // the indicator, exactly as the bar's segments do,
                            // rather than sizing to its own title. Two reasons,
                            // and the first is not cosmetic: the input region
                            // the daemon hands the compositor is the whole
                            // indicator rect, so any part of it that is not a
                            // tab swallows clicks silently. Equal shares leave
                            // no such gap. It also stops one long title from
                            // starving its siblings, which is what a row of
                            // content-sized chips does.
                            readonly property int alongAxis: pill.chipLongBudget

                            // ACROSS the axis every chip takes the pill's full
                            // inner thickness, so the run reads as one column
                            // (or row) of tabs rather than a ragged stack of
                            // differently-sized blobs. Deriving it from the
                            // pill rather than from the label is also what
                            // keeps the two radii concentric. No binding loop:
                            // the pill's thickness is the engine's number and
                            // does not depend on its content.
                            width: indicator.vertical ? pill.chipThickness : alongAxis
                            height: indicator.vertical ? alongAxis : pill.chipThickness
                            radius: root.tabRadius(pill.chipThickness)
                            color: root.tabColor(chip.modelData, false)

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.tabActivated(chip.modelData.windowId)
                            }

                            QQC2.Label {
                                id: chipLabel

                                anchors.centerIn: parent
                                // Rotated on a side edge so the title reads
                                // down the column instead of being clipped to
                                // the indicator's thickness — at any usable
                                // thickness there is no horizontal room for
                                // text there. The direction follows Qt's own
                                // vertical tab convention: bottom-to-top on a
                                // left edge, top-to-bottom on a right one, so
                                // the text always leans away from the window
                                // it labels. Rotation is about the label's
                                // centre, and the chip is sized from the same
                                // two numbers swapped, so the rotated label
                                // lands centred with no extra transform.
                                rotation: !indicator.vertical ? 0 : (indicator.leftEdge ? -90 : 90)
                                // A window with neither a registry title nor an app id would
                                // otherwise render a stray text-less blob.
                                text: chip.modelData.title || i18n("Untitled window")
                                elide: Text.ElideRight
                                // Against the chip's SHARE of the indicator,
                                // not a fixed cap (see pill.chipLongBudget).
                                width: Math.min(implicitWidth, Math.max(1, pill.chipLongBudget - Kirigami.Units.largeSpacing))
                                color: chip.isUrgent || chip.isActive ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                // pixelSize (not pointSize, which is -1 for
                                // pixel-defined theme fonts) scaled by the user's
                                // overlay font scale; the user family wins when set.
                                font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.smallFont.family
                                font.pixelSize: Math.round((Kirigami.Theme.smallFont.pixelSize > 0 ? Kirigami.Theme.smallFont.pixelSize : Kirigami.Units.gridUnit * 0.6) * root.fontSizeScale)
                                font.weight: root.fontWeight
                                font.italic: root.fontItalic
                                font.underline: root.fontUnderline
                                font.strikeout: root.fontStrikeout
                            }
                        }
                    }
                }
            }
        }
    }
}
