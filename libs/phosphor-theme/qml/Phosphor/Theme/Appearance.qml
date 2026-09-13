// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma Singleton
import QtQuick

QtObject {
    readonly property var settings: AppearanceStore.values
    readonly property bool light: settings.material === "light"
    readonly property bool compact: settings.density === "compact"
    readonly property bool bottom: settings.edge === "bottom"
    readonly property bool glass: settings.material === "glass"
    readonly property bool glow: settings.glow
    readonly property bool surfacePacks: settings.surfacePacks
    readonly property bool motion: settings.motion && !Motion.reducedMotion
    readonly property bool media: settings.media
    readonly property string visualizer: settings.visualizer
    readonly property int radius: settings.radius
    readonly property int gap: settings.gap
    readonly property int padding: compact ? 16 : 22
    readonly property int barHeight: compact ? 42 : 52
    readonly property int rowHeight: compact ? 48 : 58
    readonly property int panelWidth: 370
    readonly property color text: light ? "#172740" : (settings.palette === "ember" ? "#eee9dc" : "#e8eef9")
    readonly property color muted: light ? "#4d607a" : (settings.palette === "ember" ? "#b6b1a4" : "#97a8c0")
    readonly property color surface: light ? "#eef2fa" : (settings.palette === "ember" ? "#272620" : settings.palette === "wallpaper" ? Theme.surface_container : "#101d32")
    readonly property color card: light ? "#dce3ef" : (settings.palette === "ember" ? "#403c32" : settings.palette === "wallpaper" ? Theme.surface_container_high : "#23314a")
    readonly property color recess: light ? "#e1e8f4" : (settings.palette === "ember" ? "#1c1e1b" : "#0b1528")
    readonly property color outline: Qt.alpha(text, light ? 0.18 : 0.12)
    readonly property list<color> stops: settings.palette === "ember" ? (light ? ["#856127", "#8b5a34", "#9d4e2f", "#984350"] : ["#e8c988", "#d4b08a", "#d3906c", "#d27b83"]) : settings.palette === "wallpaper" ? [Theme.primary, Theme.secondary, Theme.tertiary, Theme.primary] : (light ? ["#137b91", "#3567b3", "#8154a8", "#a34e76"] : ["#41d4e8", "#6e9cfd", "#b68aee", "#f390b3"])
    readonly property color accent: stops[1]
    function at(position: real): color {
        const scaled = Math.max(0, Math.min(1, position)) * 3;
        const i = Math.min(2, Math.floor(scaled));
        return Qt.tint(stops[i], Qt.alpha(stops[i + 1], scaled - i));
    }
}
