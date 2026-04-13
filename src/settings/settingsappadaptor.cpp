// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsappadaptor.h"

#include "settingslaunchcontroller.h"

#include <QtGlobal>

namespace PlasmaZones {

SettingsAppAdaptor::SettingsAppAdaptor(SettingsLaunchController* launcher)
    : QDBusAbstractAdaptor(launcher)
    , m_launcher(launcher)
{
    Q_ASSERT(m_launcher);
    setAutoRelaySignals(false);
}

SettingsAppAdaptor::~SettingsAppAdaptor() = default;

void SettingsAppAdaptor::setActivePage(const QString& page)
{
    m_launcher->handleSetActivePage(page);
}

} // namespace PlasmaZones
