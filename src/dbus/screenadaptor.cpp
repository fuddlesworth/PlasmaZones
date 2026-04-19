// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenadaptor.h"

namespace PlasmaZones {

ScreenAdaptor::ScreenAdaptor(Phosphor::Screens::ScreenManager* manager, QObject* parent)
    : Phosphor::Screens::DBusScreenAdaptor(parent)
{
    // Explicit constructor injection — the caller (Daemon::init) owns the
    // ScreenManager instance and hands it to us directly. No read from
    // the `PlasmaZones::screenManager()` service-locator here: that
    // locator is a legacy-compat shim for call sites too deep in utility
    // chains to take an injected pointer, and the adaptor is neither.
    // setConfigStore() is invoked by the daemon separately with its
    // single SettingsConfigStore instance.
    setScreenManager(manager);
}

ScreenAdaptor::~ScreenAdaptor() = default;

} // namespace PlasmaZones
