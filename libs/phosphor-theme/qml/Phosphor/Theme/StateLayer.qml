// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.StateLayer, interactive-state opacity tokens.
// M3 "state layer" model. Every interactive surface paints the
// foreground color over itself at a state-specific opacity to
// communicate hover, focus, press, and drag. Centralising these here
// keeps every Pz* widget consistent. Future high-contrast and
// reduced-motion accessibility profiles can override the singleton in
// one place.

import QtQuick
pragma Singleton

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
}
