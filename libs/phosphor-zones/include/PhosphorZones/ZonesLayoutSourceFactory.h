// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorzones_export.h>

#include <PhosphorLayoutApi/ILayoutSourceFactory.h>

namespace PhosphorZones {

class IZoneLayoutRegistry;

/// Anchor symbol that forces the translation unit owning the static
/// @c LayoutSourceProviderRegistrar for this provider to be linked in.
///
/// Under @c SHARED builds (today's default) every TU of the loaded
/// library has its static initialisers run, so this anchor is a no-op.
/// The function exists so that @c STATIC builds + linker GC
/// (@c --gc-sections, @c --as-needed) can't drop
/// @c zoneslayoutsourcefactory.cpp silently — at which point the
/// static registrar never runs and the bundle ships without the zones
/// provider. Composition-root glue (@c buildStandardLayoutSourceBundle)
/// calls this once during bundle wiring. The body is an empty no-op;
/// only the symbol reference matters. See the
/// @c @@todo(plugin-compositor) note in
/// @c PhosphorLayoutApi/LayoutSourceProviderRegistry.h.
PHOSPHORZONES_EXPORT void ensureZonesLayoutSourceProviderLinked();

/// Factory for @c ZonesLayoutSource.
///
/// Composition roots register an instance with their
/// @c PhosphorLayout::LayoutSourceBundle to surface manual zone-layout
/// previews. Symmetric with @c PhosphorTiles::AutotileLayoutSourceFactory
/// — both follow the per-library factory pattern that the bundle drives
/// from one @c addFactory() line per provider.
class PHOSPHORZONES_EXPORT ZonesLayoutSourceFactory : public PhosphorLayout::ILayoutSourceFactory
{
public:
    /// @p registry is the zone-layout registry the produced source
    /// borrows. Caller owns @p registry and must keep it alive for the
    /// produced source's lifetime — typically the bundle's lifetime,
    /// which is owned by the same composition root.
    explicit ZonesLayoutSourceFactory(IZoneLayoutRegistry* registry);
    ~ZonesLayoutSourceFactory() override;

    QString name() const override;
    std::unique_ptr<PhosphorLayout::ILayoutSource> create() override;

private:
    IZoneLayoutRegistry* m_registry;
};

} // namespace PhosphorZones
