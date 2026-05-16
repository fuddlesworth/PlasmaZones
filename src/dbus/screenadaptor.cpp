// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenadaptor.h"

namespace PlasmaZones {

ScreenAdaptor::ScreenAdaptor(Phosphor::Screens::ScreenManager* manager, Phosphor::Screens::IConfigStore* store,
                             QObject* parent)
    : Phosphor::Screens::DBusScreenAdaptor(manager, store, parent)
{
}

ScreenAdaptor::~ScreenAdaptor() = default;

} // namespace PlasmaZones
