// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
 * outputs; vendor + resolution live in the tooltip.
 *
 * Pure QML drawing — no SVG asset. Reuses the proportional-geometry idiom
 * from ZonePreview/VirtualScreenPreview.
 */
ColumnLayout {
    id: root

    required property var appSettings
    // When true, collapse virtual screens ("id/vs:0") to their physical
    // parent — per-monitor settings groups always scope by physical output.
    property bool physicalOnly: true
    // Q_INVOKABLE name (on appSettings) returning bool "this screen has
    // overrides" for the page's domain, e.g. "hasPerScreenAutotileSettings".
    // Empty disables the override dots.
    property string hasOverridesMethod: ""

    readonly property string selectedScreenName: appSettings.scopeScreenName
    readonly property bool isPerScreen: selectedScreenName !== ""
    readonly property bool hasMultiple: _filteredScreens.length > 1

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
            var vsIdx = name.indexOf("/vs:");
            var physId = vsIdx >= 0 ? name.substring(0, vsIdx) : name;
            if (seen[physId])
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
                var physRes = appSettings.physicalScreenResolution(physId);
                if (physRes.width > 0 && physRes.height > 0) {
                    entry["width"] = physRes.width;
                    entry["height"] = physRes.height;
                } else {
                    delete entry["width"];
                    delete entry["height"];
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
    // every screen reports geometry we honour the true arrangement; otherwise
    // we fall back to an equal-size 16:9 row so the picker still works.
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
        if (hasOverridesMethod === "") {
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
    on_FilteredScreensChanged: _refreshOverrides()

    Connections {
        target: root.appSettings
        function onPerScreenOverridesChanged() {
            root._refreshOverrides();
        }
        // Hot-unplug: drop the scope if the selected output disappears.
        function onScreensChanged() {
            if (root.selectedScreenName === "")
                return;
            var s = root._filteredScreens;
            for (var i = 0; i < s.length; i++) {
                if (s[i].name === root.selectedScreenName)
                    return;
            }
            root.appSettings.scopeScreenName = "";
        }
    }

    spacing: Kirigami.Units.smallSpacing

    Item {
        Layout.fillWidth: true
        implicitHeight: scopeRow.implicitHeight

        RowLayout {
            id: scopeRow

            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Kirigami.Units.largeSpacing

            // "All Monitors" chip — resting default.
            Rectangle {
                Layout.preferredWidth: allContent.implicitWidth + Kirigami.Units.largeSpacing * 2
                Layout.fillHeight: true
                Layout.preferredHeight: root._mapHeight + Kirigami.Units.largeSpacing
                radius: Kirigami.Units.smallSpacing
                color: !root.isPerScreen ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.1) : allMouse.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06) : "transparent"
                border.width: Math.round(Screen.devicePixelRatio)
                border.color: !root.isPerScreen ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : allMouse.activeFocus ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                Accessible.role: Accessible.RadioButton
                Accessible.name: i18n("All Monitors")
                Accessible.checked: !root.isPerScreen

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
                    Keys.onSpacePressed: root.appSettings.scopeScreenName = ""
                    Keys.onReturnPressed: root.appSettings.scopeScreenName = ""
                    onClicked: root.appSettings.scopeScreenName = ""
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
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: connectorLabel.text
                        Accessible.checked: isSelected

                        // Connector-first label (DP-2); vendor + resolution in tooltip.
                        Label {
                            id: connectorLabel

                            anchors.centerIn: parent
                            width: parent.width - Kirigami.Units.smallSpacing * 2
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: tile.modelData.connectorName || tile.modelData.name || ""
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
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize - 2
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
                            Keys.onSpacePressed: root.appSettings.scopeScreenName = tile.screenName
                            Keys.onReturnPressed: root.appSettings.scopeScreenName = tile.screenName
                            onClicked: root.appSettings.scopeScreenName = tile.screenName
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
