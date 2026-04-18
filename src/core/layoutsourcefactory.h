// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared construction of the application-wide layout-source composite.
//
// The composite aggregates one ZonesLayoutSource (manual zone layouts) +
// one AutotileLayoutSource (autotile algorithms against the default
// AlgorithmRegistry singleton). Daemon, settings, and editor each need
// the same three-object assembly wired in the same order; centralising it
// here removes three copy-paste sites and guarantees source order cannot
// drift between processes.
//
// The returned bundle owns all three objects so callers can hold a single
// field per consumer and rely on natural destruction order.

#include "plasmazones_export.h"

#include <PhosphorLayoutApi/CompositeLayoutSource.h>

#include <memory>

namespace PhosphorZones {
class ILayoutRegistry;
class ZonesLayoutSource;
}

namespace PhosphorTiles {
class AutotileLayoutSource;
}

namespace PlasmaZones {

/// Owning bundle for one consumer's layout sources.
///
/// Declaration order is load-bearing; do not reorder. `composite` holds raw
/// pointers to `zones` and `autotile`, and C++'s reverse-declaration
/// destruction order guarantees composite is destroyed first, dropping its
/// borrowed pointers before the child sources go out of scope.
///
/// **Lifetime contract for consumers:** the bundle must outlive every
/// observer that holds `composite.get()`, `zones.get()`, or `autotile.get()`
/// — including QML model bindings, async D-Bus callbacks, queued signal
/// connections, and any cached `ILayoutSource*` stored elsewhere. Resetting
/// or moving-from a bundle while observers still hold raw pointers is a
/// use-after-free. Tear down observers (disconnect signals, drop QML
/// bindings, cancel pending D-Bus calls) before destroying the bundle.
struct PLASMAZONES_EXPORT LayoutSourceBundle
{
    LayoutSourceBundle();
    ~LayoutSourceBundle();
    LayoutSourceBundle(LayoutSourceBundle&&) noexcept;
    LayoutSourceBundle& operator=(LayoutSourceBundle&&) noexcept;
    LayoutSourceBundle(const LayoutSourceBundle&) = delete;
    LayoutSourceBundle& operator=(const LayoutSourceBundle&) = delete;

    std::unique_ptr<PhosphorZones::ZonesLayoutSource> zones;
    std::unique_ptr<PhosphorTiles::AutotileLayoutSource> autotile;
    std::unique_ptr<PhosphorLayout::CompositeLayoutSource> composite;
};

/// Build the standard {zones → autotile} composite over @p registry.
///
/// @p registry is the manual-layout registry (typically a LayoutManager);
/// caller owns it and must keep it alive for the bundle's lifetime.
///
/// The autotile source binds to @c AlgorithmRegistry::instance()
/// automatically. Caller is responsible for wiring the registry's
/// "layouts changed" signal to @c bundle.zones->notifyContentsChanged()
/// if reactive refresh is desired (the bundle can't do this itself
/// without knowing the concrete registry type).
PLASMAZONES_EXPORT LayoutSourceBundle makeLayoutSourceBundle(PhosphorZones::ILayoutRegistry* registry);

} // namespace PlasmaZones
