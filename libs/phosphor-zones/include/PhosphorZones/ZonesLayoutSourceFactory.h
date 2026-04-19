// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorzones_export.h>

#include <PhosphorLayoutApi/ILayoutSourceFactory.h>

namespace PhosphorZones {

class IZoneLayoutRegistry;

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
