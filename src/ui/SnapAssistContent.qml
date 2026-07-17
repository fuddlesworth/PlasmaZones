// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Snap Assist content body — Aero-Snap-style window picker rendered as
 * an Item inside the unified PassiveOverlayShell.
 *
 * Pre-shell (PhosphorRoles::SnapAssist surface): a standalone Window with
 * `KeyboardInteractivity::Exclusive` that received Escape via QML
 * `Shortcut`. Post-shell: an Item slot in the per-screen passive shell
 * (`KeyboardInteractivity::None`); Escape is routed via the daemon's
 * KGlobalAccel `cancel_overlay_during_drag` shortcut, which calls
 * `WindowDragAdaptor::cancelSnap()` — that already dismisses snap-assist
 * when visible, so no per-content kbd grab
 * is needed.
 *
 * Carries `property bool shaderAnchor: true` so the SurfaceAnimator's
 * recursive lookup binds the per-event transition shader to this Item's
 * layer texture rather than the entire shell scene.
 */
Item {
    id: root

    /// shaderAnchor for SurfaceAnimator. The shell wl_surface is sized
    /// to the VS rect (see `OverlayService::showSnapAssist`), so this
    /// item filling the surface naturally scopes the per-event
    /// transition shader's FBO + animation runway to the VS bounds.
    property bool shaderAnchor: true
    property var emptyZones: []
    property var candidates: []
    property int screenWidth: 1920
    property int screenHeight: 1080
    // Zone appearance defaults — C++ side overwrites from settings.
    // highlightColor is unused in-file but declared for the
    // writeColorSettings push contract symmetry (all slots receive the trio).
    property color highlightColor: QFZCommon.ZoneColorDefaults.activeZoneColor
    property color inactiveColor: QFZCommon.ZoneColorDefaults.inactiveZoneColor
    property color borderColor: QFZCommon.ZoneColorDefaults.zoneBorderColor
    property real activeOpacity: 0.5
    property real inactiveOpacity: 0.3
    property int borderWidth: Kirigami.Units.smallSpacing
    property int borderRadius: Kirigami.Units.gridUnit
    // Layout constants
    readonly property real cardScaleBase: 0.35
    readonly property real cardWidthMultiplier: 2.2
    readonly property real minCardWidth: Kirigami.Units.gridUnit * 6
    readonly property real minIconSize: Kirigami.Units.iconSizes.small
    readonly property real iconSizeRatio: 0.6
    readonly property real zoneSizeRefForFont: 200
    readonly property real minFontScale: 0.4
    readonly property real minFontPx: 8

    /// Forwarded from card click.
    signal windowSelected(string windowId, string zoneId, string geometryJson)
    /// Backdrop click / dismiss request — host side animates slot hide.
    signal dismissRequested

    anchors.fill: parent

    // Backdrop — semi-transparent dim, click outside to dismiss.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.25)

        MouseArea {
            anchors.fill: parent
            onClicked: root.dismissRequested()
            Accessible.name: i18n("Dismiss snap assist overlay")
            Accessible.role: Accessible.Button
        }
    }

    // Each zone shows all candidates; user picks any window to snap to any zone.
    Repeater {
        model: root.emptyZones

        Item {
            id: zoneContainer

            required property var modelData

            property var zone: modelData

            x: zone ? zone.x : 0
            y: zone ? zone.y : 0
            width: zone ? zone.width : 0
            height: zone ? zone.height : 0
            visible: zone && zone.zoneId && root.candidates.length > 0

            Rectangle {
                id: zoneBg

                // Shared contract with ZoneOverlayContent.hasCustomColors() and
                // RenderNodeOverlayContent's useCustom: only true, 1, or "true"
                // enable per-zone colors (raw truthiness would accept "false").
                readonly property bool useCustom: zone !== null && zone !== undefined && (zone.useCustomColors === true || zone.useCustomColors === 1 || (typeof zone.useCustomColors === "string" && zone.useCustomColors.toLowerCase() === "true"))
                readonly property color fillColor: useCustom && zone.inactiveColor ? zone.inactiveColor : root.inactiveColor
                readonly property real fillOpacity: useCustom && zone.inactiveOpacity !== undefined ? zone.inactiveOpacity : root.inactiveOpacity
                readonly property color strokeColor: useCustom && zone.borderColor ? zone.borderColor : root.borderColor
                readonly property int strokeWidth: useCustom && zone.borderWidth !== undefined ? zone.borderWidth : root.borderWidth
                readonly property int cornerRadius: useCustom && zone.borderRadius !== undefined ? zone.borderRadius : root.borderRadius

                anchors.fill: parent
                radius: zoneBg.cornerRadius
                color: Qt.rgba(zoneBg.fillColor.r, zoneBg.fillColor.g, zoneBg.fillColor.b, zoneBg.fillOpacity)
                border.color: zoneBg.strokeColor
                border.width: zoneBg.strokeWidth
            }

            Flow {
                id: candidateFlow

                readonly property real zoneSize: Math.min(zoneContainer.width, zoneContainer.height) || 1
                readonly property real cardScale: root.cardScaleBase / Math.max(1, Math.sqrt(root.candidates.length))
                readonly property real cardBaseSize: zoneSize * cardScale
                readonly property real iconSize: Math.max(root.minIconSize, cardBaseSize * root.iconSizeRatio)
                readonly property real cardWidth: Math.max(root.minCardWidth, cardBaseSize * root.cardWidthMultiplier)
                readonly property real fontPixelSize: {
                    var baseSize = Kirigami.Theme.defaultFont.pixelSize;
                    var scaleFactor = zoneSize / root.zoneSizeRefForFont;
                    var scaledSize = baseSize * Math.max(root.minFontScale, Math.min(1, scaleFactor));
                    return Math.max(root.minFontPx, Math.round(scaledSize));
                }
                readonly property real availableWidth: zoneContainer.width - Kirigami.Units.smallSpacing * 2
                readonly property real cardTotalWidth: cardWidth + Kirigami.Units.smallSpacing * 2
                readonly property real flowSpacing: Math.max(2, Math.min(8, zoneSize * 0.02))
                readonly property int itemsPerRow: Math.max(1, Math.floor((availableWidth + flowSpacing) / (cardTotalWidth + flowSpacing)))
                // Width sized to exactly one row's worth of content so
                // Flow wraps at the right item count. Paired with the
                // explicit `x` / `y` centring math below, this gives
                // the cards a stable width to centre against without
                // depending on Flow's `implicitWidth` (which fluctuates
                // with child layout passes).
                readonly property real contentWidth: {
                    var n = root.candidates.length;
                    if (n <= 0)
                        return 0;

                    var perRow = Math.min(n, itemsPerRow);
                    return perRow * cardTotalWidth + (perRow - 1) * flowSpacing;
                }

                // Explicit centring math instead of `anchors.centerIn`:
                // the Flow's `implicitHeight` is `0` until its
                // Repeater-spawned card delegates lay out, and the
                // anchor resolves once at first paint with that 0
                // height, leaving the Flow stuck at parent's `(0, 0)`
                // even after children populate. Computing `x`/`y`
                // directly from `contentWidth` / `implicitHeight`
                // re-evaluates whenever the implicit dimensions
                // settle, so the cards always land at the zone's
                // visual centre. Same approach the rest of the
                // overlay code uses for size-binding-driven
                // centring (see ZoneOverlayContent's preview cards).
                x: (zoneContainer.width - width) / 2
                y: (zoneContainer.height - implicitHeight) / 2
                width: contentWidth
                spacing: flowSpacing

                Repeater {
                    model: root.candidates

                    Item {
                        id: candidateCard

                        required property var modelData

                        property var candidate: modelData
                        property bool hovered: cardMouse.containsMouse

                        width: candidateFlow.cardWidth + Kirigami.Units.smallSpacing * 2
                        height: cardContent.height + Kirigami.Units.smallSpacing * 2

                        Rectangle {
                            // Behaviors on color / border.color were
                            // running PhosphorMotionAnimation against
                            // the initial QColor (transparent) when the
                            // async Loader instantiated this delegate;
                            // QQuickPropertyAnimation latched the start
                            // value on the first frame and the card
                            // ended up effectively transparent. Setting
                            // colors directly (no Behavior) lands the
                            // target value immediately on first paint.
                            anchors.fill: parent
                            radius: Math.max(2, candidateFlow.zoneSize * 0.01)
                            color: candidateCard.hovered ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.35) : Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.75)
                            border.color: candidateCard.hovered ? Kirigami.Theme.highlightColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                            border.width: candidateCard.hovered ? Kirigami.Units.smallSpacing : Math.max(1, Math.round(Kirigami.Units.smallSpacing / 2))
                        }

                        Row {
                            id: cardContent

                            anchors.centerIn: parent
                            width: candidateFlow.cardWidth
                            spacing: Kirigami.Units.smallSpacing

                            Item {
                                width: candidateFlow.iconSize
                                height: width

                                Image {
                                    anchors.fill: parent
                                    visible: !!(candidate && candidate.thumbnail)
                                    fillMode: Image.PreserveAspectFit
                                    source: (candidate && candidate.thumbnail) ? candidate.thumbnail : ""
                                    cache: true
                                }

                                Kirigami.Icon {
                                    anchors.fill: parent
                                    visible: !(candidate && candidate.thumbnail)
                                    source: candidate ? (candidate.icon || "application-x-executable") : "application-x-executable"
                                }
                            }

                            Label {
                                width: parent.width - candidateFlow.iconSize - Kirigami.Units.smallSpacing
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                text: candidate ? (candidate.caption || "") : ""
                                font.pixelSize: candidateFlow.fontPixelSize
                                color: Kirigami.Theme.textColor
                            }
                        }

                        MouseArea {
                            id: cardMouse

                            anchors.fill: parent
                            hoverEnabled: root.visible
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.Button
                            Accessible.name: candidate && candidate.caption ? i18n("Snap %1 to this zone", candidate.caption) : i18n("Snap window to this zone")
                            onClicked: {
                                const wId = candidate ? candidate.windowId : "";
                                const zoneId = zoneContainer.zone ? (zoneContainer.zone.zoneId || "") : "";
                                if (!zoneContainer.zone || !wId || !zoneId) {
                                    root.dismissRequested();
                                    return;
                                }
                                const z = zoneContainer.zone;
                                const geo = z && z.x !== undefined && z.y !== undefined ? JSON.stringify({
                                    "x": z.x,
                                    "y": z.y,
                                    "width": z.width,
                                    "height": z.height
                                }) : "{}";
                                root.windowSelected(wId, zoneId, geo);
                            }
                        }
                    }
                }
            }
        }
    }
}
