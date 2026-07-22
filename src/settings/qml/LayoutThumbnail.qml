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
    // aspect ratio; the min/max width clamp below can only make the box
    // narrower than this budget, never wider, so the fit stays conservative.
    readonly property real _fitBaseHeight: Math.max(Kirigami.Units.gridUnit * 3, Math.min(fitHeight - _verticalChrome, (fitWidth - _sidePadding * 2) / Math.max(0.1, layoutAspectRatio)))
    // Preview-box sizing. Height is the fixed side for every class, so a
    // portrait ratio (< 1) simply yields a narrower width, which the
    // min/max clamp below keeps usable.
    property real baseHeight: _fitMode ? _fitBaseHeight : Kirigami.Units.gridUnit * 9
    readonly property real calculatedWidth: baseHeight * layoutAspectRatio
    property real minThumbnailWidth: Kirigami.Units.gridUnit * 5 // Narrower min for portrait
    property real maxThumbnailWidth: Kirigami.Units.gridUnit * 26 // Wider max for super-ultrawide
    readonly property real previewBoxWidth: Math.max(minThumbnailWidth, Math.min(calculatedWidth, maxThumbnailWidth))
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
    implicitHeight: baseHeight + _verticalChrome
    width: implicitWidth
    height: implicitHeight

    QFZCommon.LayoutCard {
        anchors.fill: parent
        layoutData: root._cardData
        isSelected: root.isSelected
        isHovered: root.isHovered
        globalAutoAssign: root.globalAutoAssign
        previewWidth: root.previewBoxWidth
        previewHeight: root.baseHeight
        // Same feature configuration as the popup cards
        showCardBackground: true
        zonePadding: 1
        edgeGap: 1
        minZoneSize: 8
        zoneNumberDisplay: root.layout ? (root.layout.zoneNumberDisplay || "all") : "all"
        producesOverlappingZones: root.layout && root.layout.producesOverlappingZones === true
        showMasterDot: root.layout && root.layout.isAutotile === true && root.layout.supportsMasterCount === true
        masterCount: root.layout && root.layout.masterCount !== undefined ? root.layout.masterCount : 1
        // Effective settings-pipeline values — the daemon pushes the same
        // settings->activeOpacity()/inactiveOpacity()/highlightColor() into
        // the popup slots (internal.h writeColorSettings); here they come
        // from the injected settingsSource. Zone fill/border colors need no
        // override: LayoutCard's ZoneColorDefaults preview* defaults already
        // resolve through the same source.
        activeOpacity: QFZCommon.ZoneColorDefaults.settingsSource ? QFZCommon.ZoneColorDefaults.settingsSource.activeOpacity : 0.5
        inactiveOpacity: QFZCommon.ZoneColorDefaults.settingsSource ? QFZCommon.ZoneColorDefaults.settingsSource.inactiveOpacity : 0.3
        highlightColor: QFZCommon.ZoneColorDefaults.settingsSource ? QFZCommon.ZoneColorDefaults.settingsSource.highlightColor : Kirigami.Theme.highlightColor
        fontFamily: root.fontFamily
        fontSizeScale: root.fontSizeScale
        fontWeight: root.fontWeight
        fontItalic: root.fontItalic
        fontUnderline: root.fontUnderline
        fontStrikeout: root.fontStrikeout
    }
}
