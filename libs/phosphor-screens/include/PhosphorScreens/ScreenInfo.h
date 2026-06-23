// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// ScreenInfo — POD descriptor + QML-friendly serialiser.
//
// QML payload shape:
//   QML consumers binding individual keys via QVariantMap should be aware
//   that the variant map produced by screenInfoListToVariantList() emits
//   `width`, `height`, `resolution`, `x`, `y`, and `isVirtualScreen` with
//   the semantics documented in libs/phosphor-screens/CHANGES.md. In
//   particular, `isVirtualScreen` is ALWAYS present (was: only when
//   true); QML that uses `Object.keys(map).includes('isVirtualScreen')`
//   to disambiguate physical-vs-virtual must be audited. See the
//   screenInfoListToVariantList() docstring below for the full key list.
//
// Sentinel convention for width/height/virtualIndex (NOT x/y — see below):
//   - `width` / `height` use `0` (not -1) as "unknown / not yet reported".
//     The producer is expected to fill positive values when geometry is
//     known. `screenInfoListToVariantList()` skips emitting a dimension
//     when it's non-positive so a `0×0` tile doesn't render in pickers.
//   - `x` / `y` (screen-space position) have NO sentinel: 0 is a valid
//     origin and negatives are normal, so they are emitted unconditionally.
//   - `virtualIndex` uses `-1` as "not a virtual screen". `isVirtualScreen`
//     must be true and `virtualIndex >= 0` for a meaningful virtual id;
//     the serialiser warns when this invariant is violated.
//   Flipping these to negative-sentinel form would be API-breaking for
//   downstream producers, so the existing convention is locked in.

#include <QList>
#include <QString>
#include <QVariantList>

#include "phosphorscreenscore_export.h"

namespace PhosphorScreens {

/**
 * Lightweight descriptor for a connected screen, suitable for passing
 * across settings UIs and QML.
 *
 * Pure POD — no Qt meta-object plumbing. Apps that produce this data
 * (typically by querying their compositor over D-Bus or via QGuiApplication)
 * can pass lists across language boundaries through
 * screenInfoListToVariantList().
 */
struct PHOSPHORSCREENSCORE_EXPORT ScreenInfo
{
    QString name;
    bool isPrimary = false;
    QString manufacturer;
    QString model;
    int width = 0;
    int height = 0;
    /**
     * Screen-space position of the top-left corner, in the compositor's
     * global coordinate space (the same space width/height are measured in).
     * Lets a multi-monitor map lay tiles out in their real arrangement.
     * Unlike width/height, `0` is a legitimate value (the primary output
     * typically sits at the origin), so screenInfoListToVariantList() emits
     * x/y unconditionally rather than skipping non-positive values.
     */
    int x = 0;
    int y = 0;
    QString screenId;
    bool isVirtualScreen = false;
    /** Physical connector (e.g. "DP-2"). */
    QString connectorName;
    /** 0-based index within the physical screen (-1 = not virtual). */
    int virtualIndex = -1;
    /** User-facing name for virtual screens (e.g. "Left"). */
    QString virtualDisplayName;
};

/**
 * Convert a ScreenInfo list to QVariantList suitable for QML consumption.
 *
 * Each entry is a QVariantMap with keys: name, isPrimary, manufacturer,
 * model, resolution, width, height, x, y, screenId, connectorName,
 * isVirtualScreen, virtualIndex / virtualDisplayName (virtual screens only),
 * and a pre-computed displayLabel that QML selectors / context menus can
 * render without duplicating label-building logic. Always present: name,
 * isPrimary, isVirtualScreen, x, y, displayLabel. Conditional: width/height
 * (only when positive), resolution (only when both positive), manufacturer /
 * model / screenId / connectorName (only when non-empty).
 */
[[nodiscard]] PHOSPHORSCREENSCORE_EXPORT QVariantList screenInfoListToVariantList(const QList<ScreenInfo>& screens);

} // namespace PhosphorScreens
