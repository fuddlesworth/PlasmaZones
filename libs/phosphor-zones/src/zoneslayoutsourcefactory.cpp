// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ZonesLayoutSourceFactory.h>

#include <PhosphorZones/ZonesLayoutSource.h>

namespace PhosphorZones {

ZonesLayoutSourceFactory::ZonesLayoutSourceFactory(IZoneLayoutRegistry* registry)
    : m_registry(registry)
{
}

ZonesLayoutSourceFactory::~ZonesLayoutSourceFactory() = default;

QString ZonesLayoutSourceFactory::name() const
{
    return QStringLiteral("zones");
}

std::unique_ptr<PhosphorLayout::ILayoutSource> ZonesLayoutSourceFactory::create()
{
    return std::make_unique<ZonesLayoutSource>(m_registry);
}

} // namespace PhosphorZones
