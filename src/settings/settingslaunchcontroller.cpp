// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingslaunchcontroller.h"

#include "settingsappadaptor.h"
#include "settingscontroller.h"
#include "../core/constants.h"

namespace PlasmaZones {

SettingsLaunchController::SettingsLaunchController(SettingsController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_singleInstance(std::make_unique<SingleInstanceService>(SingleInstanceIds{DBus::SettingsApp::ServiceName,
                                                                                 DBus::SettingsApp::ObjectPath,
                                                                                 DBus::SettingsApp::Interface},
                                                               this))
{
    Q_ASSERT(m_controller);
    // Parent the adaptor to `this`. SingleInstanceService stored `this` as its
    // export object in the init list above, but it only dereferences it later
    // during claim() — by which point the adaptor below has been attached, so
    // Qt D-Bus's ExportAdaptors walk finds it. Destruction is automatic via
    // QObject parent/child.
    new SettingsAppAdaptor(this);
}

SettingsLaunchController::~SettingsLaunchController() = default;

bool SettingsLaunchController::registerDBusService()
{
    return m_singleInstance->claim();
}

void SettingsLaunchController::handleSetActivePage(const QString& page)
{
    m_controller->setActivePage(page);
}

} // namespace PlasmaZones
