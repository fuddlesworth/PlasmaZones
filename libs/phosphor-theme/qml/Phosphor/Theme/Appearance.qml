// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
pragma Singleton
import QtQuick

QtObject {
    readonly property var settings: AppearanceStore.values
    readonly property bool stage: settings.presentation === "stage"
    readonly property bool light: settings.material === "light"
    readonly property bool compact: settings.density === "compact"
    readonly property bool bottom: settings.edge === "bottom"
    readonly property bool glass: settings.material === "glass"
    readonly property bool glow: settings.glow
    readonly property bool surfacePacks: settings.surfacePacks
    readonly property bool motion: settings.motion && !Motion.reducedMotion
    readonly property string notificationGrouping: settings.notificationGrouping
    readonly property bool notificationPreviews: settings.notificationPreviews
    readonly property string lockLayout: settings.lockLayout
    readonly property bool lockMedia: settings.lockMedia
    readonly property bool lockNotifications: settings.lockNotifications
    readonly property bool media: settings.media
    readonly property string visualizer: settings.visualizer
    readonly property int radius: settings.radius
    readonly property int gap: settings.gap
    readonly property int padding: compact ? 16 : 22
    readonly property int barHeight: compact ? 42 : 52
    readonly property int rowHeight: compact ? 43 : 52
    readonly property int panelWidth: 364
    readonly property color text: AppearanceStore.palette.text
    readonly property color muted: AppearanceStore.palette.muted
    readonly property int barInset: settings.barInset
    readonly property int barOffset: 12
    readonly property real surfaceOpacity: AppearanceStore.palette.opacity
    readonly property color surface: AppearanceStore.palette.surface
    readonly property color card: AppearanceStore.palette.card
    readonly property color recess: AppearanceStore.palette.recess
    readonly property color outline: AppearanceStore.palette.outline
    readonly property list<color> stops: AppearanceStore.palette.stops
    readonly property color accent: stops[settings.accentIndex]
    function windowColor(index: int): color {
        return stops[[0, 2, 3, 1][Math.max(0, index) % 4]];
    }
    function at(position: real): color {
        const scaled = Math.max(0, Math.min(1, position)) * 3;
        const i = Math.min(2, Math.floor(scaled));
        return Qt.tint(stops[i], Qt.alpha(stops[i + 1], scaled - i));
    }
}
