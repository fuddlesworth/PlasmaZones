// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The pure zone-projection half of the daemon's scrolling TEMPLATE push,
// split out of scrolling.cpp so it is unit-testable without a Daemon:
// Layout* + screen rects in, the per-screen preset-list override keys out.
// updateScrollingScreens resolves WHICH template a context uses (registry
// cascade) and merges the result into the per-screen override map; this
// function owns everything in between.

#include <QRect>
#include <QVariantMap>

namespace PhosphorZones {
class Layout;
}

namespace PlasmaZones {

/// Project @p templ's zones into the strip's preset vocabulary: normalize
/// each zone against the layout's own reference basis (@p fullGeometry when
/// the layout opts into full-screen geometry, else @p availableGeometry —
/// mirroring GeometryUtils' reference selection; an invalid rect degrades
/// Zone::normalizedGeometry to the stored relative geometry), run
/// extractTemplateVocabulary, and return the ScrollPerScreenKeys preset-list
/// entries for every NON-EMPTY list. Fail-soft: a null layout, or an
/// extraction that yields no usable widths (deleted, degenerate or row-only
/// templates), returns an empty map so the engine keeps the settings preset
/// lists.
QVariantMap scrollingTemplateOverrides(const PhosphorZones::Layout* templ, const QRect& fullGeometry,
                                       const QRect& availableGeometry);

} // namespace PlasmaZones
