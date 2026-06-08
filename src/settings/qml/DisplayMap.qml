// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Spatial monitor scope selector.
 *
 * Draws the connected outputs as proportional rectangles in their real
 * screen-space arrangement (using each screen's x/y/width/height), plus an
 * "All Monitors" chip. Clicking a monitor scopes the surrounding per-monitor
 * settings to it by writing the shared `appSettings.scopeScreenName`; the
 * "All" chip clears it back to the global default.
 *
 * The selected output glows in the accent color; any output carrying
 * overrides shows a corner dot (polled via `hasOverridesMethod`). Labels lead
 * with the connector name (DP-2) the way Hyprland/niri configs reference
 * outputs; the full display label (vendor + model, or resolution as a
 * fallback) lives in the tooltip when one is known — a connector-only output
 * with no richer label suppresses the tooltip, since the face already shows it.
 *
 * Pure QML drawing — no SVG asset. Reuses the proportional-geometry idiom
 * from ZonePreview/VirtualScreenPreview.
 */
ColumnLayout {
    id: root

    // Backing object — must expose: `scopeScreenName` (string), `screens`
    // (variant list), `physicalScreenId(name)` and `physicalScreenResolution(id)`
    // (Q_INVOKABLE), plus the `perScreenOverridesChanged` and `screensChanged`
    // signals the override-dot refresh below relies on. SettingsController
    // satisfies all of these.
    required property var appSettings
    // When true, collapse virtual screens ("id/vs:0") to their physical
    // parent — per-monitor settings groups always scope by physical output.
    property bool physicalOnly: true
    // Q_INVOKABLE name (on appSettings) returning bool "this screen has
    // overrides" for the page's domain, e.g. "hasPerScreenAutotileGapsSettings".
    // Empty disables the override dots.
    property string hasOverridesMethod: ""

    // Current selection, for the highlight. The default tracks the shared scope
    // for the chip-popover use; monitor-subject hosts (Monitor State, Virtual
    // Screens) rebind this to their own local target, retiring the default.
    property string selectedScreenName: appSettings.scopeScreenName
    // Show the "All Monitors" chip. Off for monitor-subject pickers that always
    // require a specific output (Monitor State, Virtual Screens).
    property bool showAll: true
    // Emitted when the user picks a tile ("" = All Monitors). The host decides
    // what to do with it (write the shared scope, or a local target).
    // DisplayMap itself writes nothing.
    signal screenPicked(string name)
    readonly property bool isPerScreen: selectedScreenName !== ""

    // ── Screen list (physical-collapsed copy of the raw screens payload) ──
    // Imperative JS because declarative bindings can't express the
    // set-deduplication of virtual children onto their physical parent.
    readonly property var _filteredScreens: {
        var all = appSettings.screens || [];
        if (!physicalOnly)
            return all;
        var seen = {};
        var result = [];
        for (var i = 0; i < all.length; i++) {
            var name = all[i].name || "";
            var physId = appSettings.physicalScreenId(name);
            // Skip unnamed outputs: an empty physId would bucket every
            // nameless screen onto one "" tile, lossily collapsing distinct
            // outputs into a single (un-pickable) entry.
            if (!physId || seen[physId])
                continue;
            seen[physId] = true;
            var entry = {};
            for (var key in all[i])
                entry[key] = all[i][key];
            entry["name"] = physId;
            if (all[i].isVirtualScreen) {
                delete entry["isVirtualScreen"];
                delete entry["virtualIndex"];
                delete entry["virtualDisplayName"];
                // The serializer precomputed displayLabel/resolution from the
                // VIRTUAL child (e.g. "VS1 — …" with half-width geometry). Drop
                // them and rebuild for the demoted PHYSICAL entry below, so the
                // tooltip/label never surfaces a virtual-screen label.
                delete entry["displayLabel"];
                delete entry["resolution"];
                var physRes = appSettings.physicalScreenResolution(physId);
                if (physRes.width > 0 && physRes.height > 0) {
                    entry["width"] = physRes.width;
                    entry["height"] = physRes.height;
                    entry["resolution"] = physRes.width + "×" + physRes.height;
                } else {
                    delete entry["width"];
                    delete entry["height"];
                }
                // Rebuild a physical displayLabel from the surviving
                // manufacturer/model (they identify the physical monitor, shared
                // by every virtual child) + the physical resolution, so the
                // demoted tile's tooltip reads like a real physical screen
                // ("LG UltraFine (3840×2160)") instead of bare resolution. With
                // no vendor/model the tooltip falls back to resolution and the
                // tile leads with connectorName.
                var vendorModel = [entry["manufacturer"], entry["model"]].filter(Boolean).join(" ");
                if (vendorModel) {
                    var rebuilt = vendorModel + (entry["resolution"] ? " (" + entry["resolution"] + ")" : "");
                    // Mirror the C++ builder's ` · <connector>` disambiguation
                    // suffix (appended whenever the label carries a make/model)
                    // so two identical make/model/resolution panels stay
                    // distinguishable in the tooltip.
                    if (entry["connectorName"])
                        rebuilt += " · " + entry["connectorName"];
                    entry["displayLabel"] = rebuilt;
                }
            }
            result.push(entry);
        }
        return result;
    }

    // ── Proportional layout maths ──
    readonly property real _mapHeight: Kirigami.Units.gridUnit * 4
    readonly property real _maxMapWidth: Kirigami.Units.gridUnit * 26
    // Per-tile pixel rects, scaled from real geometry into the map band. When
    // every screen reports geometry we honour the true arrangement; if even one
    // screen lacks geometry (e.g. a virtual screen whose physical parent can't
    // be resolved while the daemon is offline) the whole map falls back to an
    // equal-size 16:9 row so the picker still works — proportional and equal
    // tiles are never mixed in one map.
    readonly property var _tileRects: {
        var s = _filteredScreens;
        var n = s.length;
        if (n === 0)
            return [];

        var haveGeo = true;
        for (var i = 0; i < n; i++) {
            if (!(s[i].width > 0 && s[i].height > 0)) {
                haveGeo = false;
                break;
            }
        }

        if (haveGeo) {
            var minX = s[0].x, minY = s[0].y, maxX = s[0].x + s[0].width, maxY = s[0].y + s[0].height;
            for (var j = 1; j < n; j++) {
                minX = Math.min(minX, s[j].x);
                minY = Math.min(minY, s[j].y);
                maxX = Math.max(maxX, s[j].x + s[j].width);
                maxY = Math.max(maxY, s[j].y + s[j].height);
            }
            var bw = maxX - minX, bh = maxY - minY;
            if (bw > 0 && bh > 0) {
                var scale = _mapHeight / bh;
                if (bw * scale > _maxMapWidth)
                    scale = _maxMapWidth / bw;
                var rects = [];
                for (var k = 0; k < n; k++)
                    rects.push({
                        "x": (s[k].x - minX) * scale,
                        "y": (s[k].y - minY) * scale,
                        "w": s[k].width * scale,
                        "h": s[k].height * scale
                    });
                return rects;
            }
        }

        // Fallback: equal 16:9 tiles in a row.
        var tileH = _mapHeight;
        var tileW = Math.round(tileH * 16 / 9);
        var gap = Kirigami.Units.smallSpacing;
        var fb = [];
        for (var m = 0; m < n; m++)
            fb.push({
                "x": m * (tileW + gap),
                "y": 0,
                "w": tileW,
                "h": tileH
            });
        return fb;
    }
    readonly property real _contentW: {
        var r = _tileRects, w = 0;
        for (var i = 0; i < r.length; i++)
            w = Math.max(w, r[i].x + r[i].w);
        return w;
    }
    readonly property real _contentH: {
        var r = _tileRects, h = 0;
        for (var i = 0; i < r.length; i++)
            h = Math.max(h, r[i].y + r[i].h);
        return h;
    }

    // ── Override presence per screen (name -> bool) ──
    property var _overrides: ({})
    function _refreshOverrides() {
        // Dots key by the physical-collapsed `name`, so they only make sense
        // when physicalOnly is true (per-monitor settings groups always scope
        // by physical output). Disable them otherwise rather than mis-keying a
        // raw virtual id against a physical-scoped override lookup.
        if (hasOverridesMethod === "" || !physicalOnly) {
            _overrides = {};
            return;
        }
        var s = _filteredScreens, map = {};
        for (var i = 0; i < s.length; i++)
            map[s[i].name] = appSettings[hasOverridesMethod](s[i].name) === true;
        _overrides = map;
    }
    Component.onCompleted: _refreshOverrides()
    onHasOverridesMethodChanged: _refreshOverrides()

    Connections {
        target: root.appSettings
        function onPerScreenOverridesChanged() {
            root._refreshOverrides();
        }
        // Screen list changed (hot-plug, virtual-screen reshape) → re-poll the
        // per-output override dots. Driven off the real screensChanged signal
        // rather than a _filteredScreens property change-handler, whose handler
        // name is easy to mis-case and then silently never fire.
        function onScreensChanged() {
            root._refreshOverrides();
        }
    }

    spacing: Kirigami.Units.smallSpacing

    Item {
        Layout.fillWidth: true
        // Definite implicit width so the ColumnLayout reports a real size when
        // hosted in a content-sized container (e.g. the scope-chip popover).
        implicitWidth: scopeRow.implicitWidth
        implicitHeight: scopeRow.implicitHeight

        RowLayout {
            id: scopeRow

            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Kirigami.Units.largeSpacing

            // "All Monitors" chip — resting default.
            Rectangle {
                visible: root.showAll
                Layout.preferredWidth: allContent.implicitWidth + Kirigami.Units.largeSpacing * 2
                Layout.fillHeight: true
                Layout.preferredHeight: root._mapHeight + Kirigami.Units.largeSpacing
                radius: Kirigami.Units.smallSpacing
                color: !root.isPerScreen ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.1) : allMouse.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06) : "transparent"
                border.width: Math.round(Screen.devicePixelRatio)
                border.color: !root.isPerScreen ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : allMouse.activeFocus ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                ColumnLayout {
                    id: allContent

                    anchors.centerIn: parent
                    spacing: Kirigami.Units.smallSpacing / 2

                    Kirigami.Icon {
                        source: "computer"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                        Layout.alignment: Qt.AlignHCenter
                        opacity: !root.isPerScreen ? 1 : 0.5
                    }

                    Label {
                        text: i18n("All Monitors")
                        font: Kirigami.Theme.smallFont
                        Layout.alignment: Qt.AlignHCenter
                        opacity: !root.isPerScreen ? 1 : 0.5
                    }
                }

                MouseArea {
                    id: allMouse

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    activeFocusOnTab: true
                    // a11y role lives on the focusable item (this MouseArea), so
                    // assistive tech sees the radio role and the focus together.
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: i18n("All Monitors")
                    Accessible.checked: !root.isPerScreen
                    Keys.onSpacePressed: root.screenPicked("")
                    Keys.onReturnPressed: root.screenPicked("")
                    onClicked: root.screenPicked("")
                }

                Behavior on color {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }
                Behavior on border.color {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }
            }

            // Proportional monitor map.
            Item {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: root._contentW
                implicitHeight: root._contentH

                Repeater {
                    model: root._filteredScreens

                    delegate: Rectangle {
                        id: tile

                        required property var modelData
                        required property int index
                        readonly property string screenName: modelData.name || ""
                        readonly property bool isSelected: root.selectedScreenName === screenName
                        readonly property bool hasOverride: root._overrides[screenName] === true
                        readonly property var rect: root._tileRects[index] || {
                            "x": 0,
                            "y": 0,
                            "w": 0,
                            "h": 0
                        }

                        x: rect.x
                        y: rect.y
                        width: rect.w
                        height: rect.h
                        radius: Kirigami.Units.smallSpacing
                        color: isSelected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.18) : tileMouse.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04)
                        border.width: isSelected ? Math.round(Screen.devicePixelRatio) * 2 : Math.round(Screen.devicePixelRatio)
                        border.color: isSelected ? Kirigami.Theme.highlightColor : tileMouse.activeFocus ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)

                        // Connector-first label (DP-2); vendor + resolution in tooltip.
                        Label {
                            id: connectorLabel

                            anchors.centerIn: parent
                            width: parent.width - Kirigami.Units.smallSpacing * 2
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            // Connector first; fall back to the friendly display
                            // label (vendor + model) before the raw screen id so
                            // an output with no connector name still reads sanely.
                            text: tile.modelData.connectorName || tile.modelData.displayLabel || tile.modelData.name || ""
                            font: Kirigami.Theme.smallFont
                            opacity: tile.isSelected ? 1 : 0.7
                        }

                        // Primary badge.
                        Rectangle {
                            visible: tile.modelData.isPrimary || false
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: Kirigami.Units.smallSpacing / 2
                            width: primaryLabel.implicitWidth + Kirigami.Units.smallSpacing
                            height: primaryLabel.implicitHeight + Kirigami.Units.smallSpacing / 2
                            radius: height / 2
                            color: Qt.rgba(Kirigami.Theme.positiveTextColor.r, Kirigami.Theme.positiveTextColor.g, Kirigami.Theme.positiveTextColor.b, 0.18)

                            Label {
                                id: primaryLabel

                                anchors.centerIn: parent
                                text: i18nc("@label primary monitor badge", "Primary")
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.positiveTextColor
                            }
                        }

                        // Override marker.
                        Rectangle {
                            visible: tile.hasOverride
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: Kirigami.Units.smallSpacing / 2
                            width: Kirigami.Units.smallSpacing * 1.5
                            height: width
                            radius: width / 2
                            color: Kirigami.Theme.highlightColor
                            border.width: Math.round(Screen.devicePixelRatio)
                            border.color: Kirigami.Theme.backgroundColor
                        }

                        MouseArea {
                            id: tileMouse

                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            activeFocusOnTab: true
                            // a11y role on the focusable item, not the visual tile.
                            Accessible.role: Accessible.RadioButton
                            // Fall back so a screen reader never announces an empty
                            // name for an output with no connector/label/name.
                            Accessible.name: connectorLabel.text || i18n("Unknown monitor")
                            Accessible.checked: tile.isSelected
                            Keys.onSpacePressed: root.screenPicked(tile.screenName)
                            Keys.onReturnPressed: root.screenPicked(tile.screenName)
                            onClicked: root.screenPicked(tile.screenName)
                        }

                        ToolTip.visible: tileMouse.containsMouse && ToolTip.text !== ""
                        ToolTip.text: {
                            var parts = [];
                            if (tile.modelData.displayLabel)
                                parts.push(tile.modelData.displayLabel);
                            else if (tile.modelData.resolution)
                                parts.push(tile.modelData.resolution);
                            return parts.join("\n");
                        }

                        Behavior on color {
                            PhosphorMotionAnimation {
                                profile: "widget.hover"
                                durationOverride: Kirigami.Units.shortDuration
                            }
                        }
                        Behavior on border.color {
                            PhosphorMotionAnimation {
                                profile: "widget.hover"
                                durationOverride: Kirigami.Units.shortDuration
                            }
                        }
                    }
                }
            }
        }
    }
}
