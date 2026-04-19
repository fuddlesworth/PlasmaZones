// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/AutotileLayoutSourceFactory.h>

#include <PhosphorTiles/AutotileLayoutSource.h>

namespace PhosphorTiles {

AutotileLayoutSourceFactory::AutotileLayoutSourceFactory(ITileAlgorithmRegistry* registry)
    : m_registry(registry)
{
}

AutotileLayoutSourceFactory::~AutotileLayoutSourceFactory() = default;

QString AutotileLayoutSourceFactory::name() const
{
    return QStringLiteral("autotile");
}

std::unique_ptr<PhosphorLayout::ILayoutSource> AutotileLayoutSourceFactory::create()
{
    return std::make_unique<AutotileLayoutSource>(m_registry);
}

} // namespace PhosphorTiles
