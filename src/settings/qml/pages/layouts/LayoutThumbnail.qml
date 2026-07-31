// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Thumbnail preview of a layout — thin wrapper around the shared
 * QFZCommon.LayoutCard so the settings app renders the exact same stack
 * (face color, zone fills, selection state, label/badge row) as the
 * daemon's layout-picker and zone-selector popups.
 *
 * This component only owns the sizing policy: it computes a preview box
 * at the layout's intended aspect ratio (16:9, 21:9, 32:9, 9:16, or the
 * target screen's ratio) and reserves the same chrome budget around it
 * as the popup cards (LayoutPickerContent metrics: 1 gridUnit side
 * padding, 3 gridUnits vertical chrome). Everything visual is LayoutCard.
 *
 * Zone colors and opacities are NOT passed explicitly — LayoutCard's
 * ZoneColorDefaults preview* defaults resolve through the injected
 * `settingsSource` (Main.qml) to the same effective settings-pipeline
 * values the daemon pushes into the popup slots. Only the opacities and
 * the card-state highlight need the explicit settingsSource read because
 * LayoutCard has no singleton default for them.
 */
Item {
    id: root

    required property var layout
    property bool isSelected: false
    property bool isHovered: false
    // Mirrors the global "Auto-assign for all layouts" master toggle so the
    // built-in CategoryBadge shows effective state (same as the popups).
    property bool globalAutoAssign: false
    // Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    // Smallest a zone rect may render at. ZonePreview hides a zone's number
    // below 16px in either direction, so a host whose zones must stay
    // number-readable (the scrolling strip, where every tile is a digit
    // target) raises this to 16.
    property int minZoneSize: 8
    // Zone fill opacities when no settings source is attached — the same
    // values LayoutCard's own activeOpacity / inactiveOpacity defaults carry,
    // named here so the pair stays in step by reference rather than by luck.
    readonly property real _fallbackActiveOpacity: 0.5
    readonly property real _fallbackInactiveOpacity: 0.3
    // Override: set to a positive value to force the aspect ratio for the target screen
    // (e.g., a virtual screen that is portrait even though the primary display is landscape).
    property real screenAspectRatio: 0
    // Aspect ratio: use the layout's intended ratio so previews show correct proportions.
    // Falls back to primary screen ratio for user-created layouts (aspectRatioClass "any" or absent).
    readonly property real fallbackAspectRatio: screenAspectRatio > 0 ? screenAspectRatio : ((Screen.width > 0 && Screen.height > 0) ? (Screen.width / Screen.height) : (16 / 9))
    readonly property real layoutAspectRatio: {
        // If a specific screen aspect ratio was provided, use it — the preview
        // box matches the physical monitor shape; LayoutCard letterboxes
        // class layouts inside it exactly like the popups do.
        if (root.screenAspectRatio > 0)
            return root.screenAspectRatio;

        var cls = root.layout ? (root.layout.aspectRatioClass || "any") : "any";
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
            // For "any" layouts with fixed-geometry zones, use the reference
            // aspect ratio from the screen the zones were designed for
            var refAR = root.layout ? (root.layout.referenceAspectRatio || 0) : 0;
            return refAR > 0 ? refAR : fallbackAspectRatio;
        }
    }
    // Opt-in fit sizing: when both are set (> 0) the preview box derives its
    // height from the given bounds instead of the fixed default, so the zone
    // graphic resizes with the host card while the label/badge text keeps its
    // natural pixel size. Hosts that previously shrank the whole thumbnail
    // with a scale transform (which also shrank the text into illegibility)
    // should use this instead.
    property real fitWidth: 0
    property real fitHeight: 0
    readonly property bool _fitMode: fitWidth > 0 && fitHeight > 0
    // The tallest preview that fits both bounds once the side padding and
    // vertical chrome are reserved. The width-derived bound divides by the
    // aspect ratio.
    // The lower bound only guards against degenerate (zero/negative) budgets
    // during layout; it must stay below every realistic budget or the box
    // overflows the very bounds it is meant to fit (a gridUnit*3 floor already
    // exceeds the 32:9 width budget at minimum card width).
    readonly property real _fitBaseHeight: Math.max(Kirigami.Units.gridUnit, Math.min(fitHeight - _verticalChrome, (fitWidth - _sidePadding * 2) / Math.max(0.1, layoutAspectRatio)))
    // Preview-box sizing runs WIDTH first: baseHeight is the height budget, it
    // yields a width, the min/max clamp settles the final width, and the box
    // height is derived back from that width. Deriving the height last is what
    // keeps the requested aspect ratio through both clamps — holding the height
    // fixed while the max clamp narrowed the box drew an ultrawide strip in a
    // box too tall for it, so its tiles rendered wider than the windows they
    // stand for.
    property real baseHeight: _fitMode ? _fitBaseHeight : Kirigami.Units.gridUnit * 9
    readonly property real calculatedWidth: baseHeight * layoutAspectRatio
    property real minThumbnailWidth: Kirigami.Units.gridUnit * 5 // Narrower min for portrait
    property real maxThumbnailWidth: Kirigami.Units.gridUnit * 26 // Wider max for super-ultrawide
    // In fit mode the min clamp must not widen the box past the width the
    // bounds actually offer: a portrait layout in a narrow card has a width
    // budget below minThumbnailWidth, and widening to that floor overflowed the
    // very card the thumbnail was asked to fit.
    readonly property real _fitWidthBudget: Math.max(1, fitWidth - _sidePadding * 2)
    readonly property real previewBoxWidth: {
        // No minThumbnailWidth floor in fit mode. The height is DERIVED from
        // the width, so a floor that widens a portrait box also TALLENS it —
        // past the fitHeight budget the box was asked to honour (a 9:16
        // layout floored to 90px wide is 160px tall in a 110px budget,
        // spilling over the card). Without the floor, width ≤ calculatedWidth
        // keeps height ≤ _fitBaseHeight, which is bounded by the fit budget
        // by construction; the fit budgets are the only bounds fit mode
        // needs.
        if (_fitMode)
            return Math.min(calculatedWidth, maxThumbnailWidth, _fitWidthBudget);
        return Math.max(minThumbnailWidth, Math.min(calculatedWidth, maxThumbnailWidth));
    }
    readonly property real previewBoxHeight: previewBoxWidth / Math.max(0.1, layoutAspectRatio)
    // Chrome budget around the preview box — matches the popup card cell
    // (LayoutPickerContent: cardWidth = preview + paddingSide*2,
    // cardHeight = preview + containerPadding + paddingSide).
    readonly property real _sidePadding: Kirigami.Units.gridUnit
    readonly property real _verticalChrome: Kirigami.Units.gridUnit * 3
    // Layout data with the legacy "Unnamed" fallback the old thumbnail label
    // showed — LayoutCard renders layoutData.displayName verbatim.
    readonly property var _cardData: {
        var l = root.layout || {};
        if (l.displayName)
            return l;

        return Object.assign({}, l, {
            "displayName": i18n("Unnamed")
        });
    }

    implicitWidth: previewBoxWidth + _sidePadding * 2
    implicitHeight: previewBoxHeight + _verticalChrome
    // A bare Item is exposed with NoRole, which assistive technology skips
    // along with any Accessible.name a host set on it. Declaring the role makes
    // the thumbnail an announceable graphic, which is what hosts that name it
    // are relying on.
    // Conditional together with the card's Accessible.ignored below: a host
    // that names the thumbnail gets a named Graphic wrapping an ignored
    // card; an unnamed host keeps the card's own Pane announcement and this
    // root stays out of the accessibility tree.
    Accessible.role: root.Accessible.name !== "" ? Accessible.Graphic : Accessible.NoRole

    QFZCommon.LayoutCard {
        anchors.fill: parent
        // The card carries its own Pane name (the layout's display name). When
        // the host named the thumbnail itself, that Pane would be announced
        // instead of the host's name, so step aside for it. Hosts that leave
        // the thumbnail unnamed (the layout grid) still rely on the card name,
        // so this must stay conditional.
        Accessible.ignored: root.Accessible.name !== ""
        layoutData: root._cardData
        isSelected: root.isSelected
        isHovered: root.isHovered
        globalAutoAssign: root.globalAutoAssign
        previewWidth: root.previewBoxWidth
        previewHeight: root.previewBoxHeight
        // Same feature configuration as the popup cards. The three pixel
        // literals are deliberate overrides of LayoutCard's own defaults
        // (2 / 2 / 10): the settings thumbnail draws a denser grid than a
        // popup card, so the gaps are halved and the zone floor lowered.
        // Hosts that need a different floor set minZoneSize on the thumbnail.
        showCardBackground: true
        zonePadding: 1
        edgeGap: 1
        minZoneSize: root.minZoneSize
        zoneNumberDisplay: root.layout ? (root.layout.zoneNumberDisplay || "all") : "all"
        producesOverlappingZones: root.layout ? root.layout.producesOverlappingZones === true : false
        showMasterDot: root.layout ? (root.layout.isAutotile === true && root.layout.supportsMasterCount === true) : false
        masterCount: root.layout && root.layout.masterCount !== undefined ? root.layout.masterCount : 1
        // Effective settings-pipeline values — the daemon pushes the same
        // settings->activeOpacity()/inactiveOpacity()/highlightColor() into
        // the popup slots (internal.h writeColorSettings); here they come
        // from the injected settingsSource. Zone fill/border colors need no
        // override: LayoutCard's ZoneColorDefaults preview* defaults already
        // resolve through the same source.
        activeOpacity: QFZCommon.ZoneColorDefaults.settingsSource ? QFZCommon.ZoneColorDefaults.settingsSource.activeOpacity : root._fallbackActiveOpacity
        inactiveOpacity: QFZCommon.ZoneColorDefaults.settingsSource ? QFZCommon.ZoneColorDefaults.settingsSource.inactiveOpacity : root._fallbackInactiveOpacity
        highlightColor: QFZCommon.ZoneColorDefaults.settingsSource ? QFZCommon.ZoneColorDefaults.settingsSource.highlightColor : Kirigami.Theme.highlightColor
        fontFamily: root.fontFamily
        fontSizeScale: root.fontSizeScale
        fontWeight: root.fontWeight
        fontItalic: root.fontItalic
        fontUnderline: root.fontUnderline
        fontStrikeout: root.fontStrikeout
    }
}
