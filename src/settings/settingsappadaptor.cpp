// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsappadaptor.h"

#include "settingscontroller.h"

namespace PlasmaZones {

SettingsAppAdaptor::SettingsAppAdaptor(SettingsController* controller)
    : QDBusAbstractAdaptor(controller)
    , m_controller(controller)
{
    setAutoRelaySignals(false);
}

SettingsAppAdaptor::~SettingsAppAdaptor() = default;

void SettingsAppAdaptor::setActivePage(const QString& page)
{
    if (m_controller) {
        m_controller->setActivePage(page);
    }
}

} // namespace PlasmaZones
