// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRegistry/RegistryNotifier.h>

namespace PhosphorRegistry {

RegistryNotifier::RegistryNotifier(QObject* parent)
    : QObject(parent)
{
}

RegistryNotifier::~RegistryNotifier() = default;

} // namespace PhosphorRegistry
