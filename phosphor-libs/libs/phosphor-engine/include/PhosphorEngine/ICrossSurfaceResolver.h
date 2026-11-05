// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QString>

namespace PhosphorEngine {

/**
 * @brief Resolves the neighbouring surface — output or virtual desktop — in a
 *        direction, for cross-surface window navigation.
 *
 * Implemented daemon-side over the screen-topology and virtual-desktop services
 * and injected into the placement engines, so the geometry/desktop knowledge
 * lives in exactly one place and the autotile and snap engines resolve
 * crossings identically (the engines just ask). Both methods take the
 * lower-case direction tokens "left" / "right" / "up" / "down".
 */
class ICrossSurfaceResolver
{
public:
    virtual ~ICrossSurfaceResolver() = default;

    /// The connected output geometrically adjacent to @p screenId in
    /// @p direction, or an empty string when there is none.
    virtual QString neighborOutputInDirection(const QString& screenId, const QString& direction) const = 0;

    /// The 1-based virtual desktop reached by stepping @p direction from
    /// @p currentDesktop on the desktop grid, or 0 when there is none.
    virtual int neighborDesktopInDirection(int currentDesktop, const QString& direction) const = 0;
};

} // namespace PhosphorEngine
