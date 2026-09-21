// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PlasmaZones {

/// Palette-following colours: resolve one of the theme-fallback roles from
/// the live application palette (with the ZoneDefaults alphas), falling back
/// to the ConfigDefaults constants when the process cannot observe a palette
/// — no GUI application (headless config tools), or a caller off the GUI
/// thread. The first four are the zone overlay's; DropIndicator is the
/// scrolling drop target's, which takes the same palette Highlight but
/// OPAQUE — its fill alpha comes from the opacity slider and its border has
/// no slider at all.
enum class SystemColorRole {
    Highlight,
    Inactive,
    Border,
    LabelFont,
    DropIndicator
};

} // namespace PlasmaZones
