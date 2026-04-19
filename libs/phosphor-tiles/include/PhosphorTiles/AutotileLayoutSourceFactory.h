// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

#include <PhosphorLayoutApi/ILayoutSourceFactory.h>

namespace PhosphorTiles {

class ITileAlgorithmRegistry;

/// Factory for @c AutotileLayoutSource.
///
/// Composition roots register an instance with their
/// @c PhosphorLayout::LayoutSourceBundle to surface autotile-algorithm
/// previews. Symmetric with @c PhosphorZones::ZonesLayoutSourceFactory
/// — both follow the per-library factory pattern that the bundle drives
/// from one @c addFactory() line per provider.
class PHOSPHORTILES_EXPORT AutotileLayoutSourceFactory : public PhosphorLayout::ILayoutSourceFactory
{
public:
    /// @p registry is the tile-algorithm registry the produced source
    /// borrows. Caller owns @p registry and must keep it alive for the
    /// produced source's lifetime — typically the bundle's lifetime,
    /// which is owned by the same composition root.
    explicit AutotileLayoutSourceFactory(ITileAlgorithmRegistry* registry);
    ~AutotileLayoutSourceFactory() override;

    QString name() const override;
    std::unique_ptr<PhosphorLayout::ILayoutSource> create() override;

private:
    ITileAlgorithmRegistry* m_registry;
};

} // namespace PhosphorTiles
