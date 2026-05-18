// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/NavigationContext.h>
#include <phosphorengine_export.h>

#include <QtGlobal>

namespace PhosphorEngine {

/// Scrollable-tiling (niri-style) navigation operations.
///
/// These column/strip operations have no equivalent among the generic
/// IPlacementEngine navigation intents and no meaning for the snap or
/// autotile engines — neither has a notion of columns or an unbounded
/// strip. Rather than burden IPlacementEngine with no-op virtuals that the
/// other two engines would inherit purely as dead weight (an Interface
/// Segregation violation), the scroll engine implements this companion
/// interface in addition to IPlacementEngine.
///
/// Callers resolve it with `dynamic_cast<IScrollNavigation*>(engine)` and
/// skip the operation when the cast yields nullptr — i.e. when the focused
/// screen is not a scroll screen. The class is exported so its typeinfo has
/// default visibility and the cross-cast works across shared-library
/// boundaries.
class PHOSPHORENGINE_EXPORT IScrollNavigation
{
public:
    virtual ~IScrollNavigation() = default;

    /// Pull the focused window of the next column into the focused column.
    virtual void consumeWindowIntoColumn(const NavigationContext& ctx) = 0;
    /// Push the focused window out of its column into a new column of its own.
    virtual void expelWindowFromColumn(const NavigationContext& ctx) = 0;
    /// Cycle the focused column's width through the configured width presets.
    virtual void cyclePresetColumnWidth(const NavigationContext& ctx) = 0;
    /// Cycle the focused window's height through the configured height presets.
    virtual void cyclePresetWindowHeight(const NavigationContext& ctx) = 0;
    /// Toggle the focused column between full viewport width and its prior width.
    virtual void toggleColumnFullWidth(const NavigationContext& ctx) = 0;
    /// Adjust the focused column's width by @p deltaFraction of the working
    /// area (positive grows the column, negative shrinks it).
    virtual void adjustColumnWidth(qreal deltaFraction, const NavigationContext& ctx) = 0;
};

} // namespace PhosphorEngine
