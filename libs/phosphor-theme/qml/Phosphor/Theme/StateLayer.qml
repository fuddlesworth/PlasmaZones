// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.StateLayer, interactive-state opacity tokens.
// M3 "state layer" model. Every interactive surface paints the
// foreground color over itself at a state-specific opacity to
// communicate hover, focus, press, and drag. Centralising these here
// keeps every Phosphor* widget consistent. Future high-contrast and
// reduced-motion accessibility profiles can override the singleton in
// one place.

pragma Singleton

import QtQuick

QtObject {
    // M3 spec values, decimal opacity from 0 to 1.
    readonly property real hover: 0.08
    readonly property real focus: 0.12
    readonly property real pressed: 0.12
    readonly property real dragged: 0.16
    // Disabled-content opacity. This is applied to the FOREGROUND color
    // rather than as a state layer painted over the surface.
    readonly property real disabled_content: 0.38
    readonly property real disabled_container: 0.12
    // Secondary-content opacity, for a supporting line that must ride the
    // SAME foreground colour as its primary rather than take a theme role
    // of its own. A tile whose container flips to `primary` on activation
    // needs its sublabel to follow to `on_primary`, and no static role
    // pairing guarantees contrast across both states.
    readonly property real secondary_content: 0.75

    // Disabled-state tints: the given foreground colour at the M3 disabled
    // opacity. Pass the colour in (rather than reading a Theme role here) so
    // the caller's binding still tracks the source colour and retints live
    // on a palette change. Use disabledContent for text/icons/handles and
    // disabledContainer for fills/outlines.
    function disabledContent(color) {
        return Qt.rgba(color.r, color.g, color.b, disabled_content);
    }
    function disabledContainer(color) {
        return Qt.rgba(color.r, color.g, color.b, disabled_container);
    }
}
