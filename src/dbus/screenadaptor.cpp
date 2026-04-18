// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenadaptor.h"

#include "../config/settings.h"
#include "../config/settingsconfigstore.h"
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
}

ScreenAdaptor::~ScreenAdaptor() = default;

void ScreenAdaptor::setSettings(Settings* settings)
{
    // Build a backing SettingsConfigStore and hand a non-owning pointer
    // to the base adaptor. Reset rather than reuse to handle a re-wire
    // in tests.
    m_virtualScreenStore = settings ? std::make_unique<SettingsConfigStore>(settings) : nullptr;
    setConfigStore(m_virtualScreenStore.get());
}

} // namespace PlasmaZones
