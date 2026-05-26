// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.StateLayer, interactive-state opacity tokens.
// M3 "state layer" model: every interactive surface paints the
// foreground color over itself at a state-specific opacity to
// communicate hover / focus / press / drag. Centralising these here
// keeps every Pz* widget consistent and lets accessibility profiles
// (high-contrast, reduced motion) tweak them in one place.

import QtQuick
pragma Singleton

QtObject {
    // M3 spec values (decimal opacity, 0-1).
    readonly property real hover: 0.08
    readonly property real focus: 0.12
    readonly property real pressed: 0.12
    readonly property real dragged: 0.16
    // Disabled-content opacity (applied to the FOREGROUND color, not as
    // a state layer over the surface).
    readonly property real disabled_content: 0.38
    readonly property real disabled_container: 0.12
}
