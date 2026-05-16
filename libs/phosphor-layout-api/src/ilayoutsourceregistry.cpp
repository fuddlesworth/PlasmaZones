// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/ILayoutSourceRegistry.h>

namespace PhosphorLayout {

ILayoutSourceRegistry::ILayoutSourceRegistry(QObject* parent)
    : QObject(parent)
{
}

ILayoutSourceRegistry::~ILayoutSourceRegistry() = default;

} // namespace PhosphorLayout
