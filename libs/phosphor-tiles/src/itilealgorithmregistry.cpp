// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/ITileAlgorithmRegistry.h>

namespace PhosphorTiles {

ITileAlgorithmRegistry::ITileAlgorithmRegistry(QObject* parent)
    : PhosphorLayout::ILayoutSourceRegistry(parent)
{
}

ITileAlgorithmRegistry::~ITileAlgorithmRegistry() = default;

} // namespace PhosphorTiles
