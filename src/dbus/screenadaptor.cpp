// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenadaptor.h"

#include "../core/screenmanagerservice.h"

namespace PlasmaZones {

ScreenAdaptor::ScreenAdaptor(QObject* parent)
    : Phosphor::Screens::DBusScreenAdaptor(parent)
{
    // Wire up the process-global ScreenManager pointer as soon as the
    // adaptor is constructed. The daemon's Daemon ctor calls
    // setScreenManager() with its m_screenManager.get() before any
    // D-Bus queries can land (D-Bus registration happens during init()),
    // so the value we read here is authoritative.
    setScreenManager(screenManager());
    // setConfigStore() is invoked directly by the daemon with its
    // SettingsConfigStore after construction — keeping a single store
    // instance per process so VS writes go through one change-signal
    // channel instead of the adaptor owning a parallel store backed
    // by the same Settings.
}

ScreenAdaptor::~ScreenAdaptor() = default;

} // namespace PlasmaZones
