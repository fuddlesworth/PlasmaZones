// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingslaunchcontroller.h"

#include "settingsappadaptor.h"
#include "settings/controller/settingscontroller.h"

#include <PhosphorProtocol/ServiceConstants.h>

namespace PlasmaZones {

SettingsLaunchController::SettingsLaunchController(SettingsController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_singleInstance(std::make_unique<SingleInstanceService>(
          SingleInstanceIds{PhosphorProtocol::Service::Apps::Settings::ServiceName,
                            PhosphorProtocol::Service::Apps::Settings::ObjectPath,
                            PhosphorProtocol::Service::Apps::Settings::Interface},
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
    // `page` is an address that MAY carry a "#anchor" deep-link fragment
    // (the D-Bus method name is unchanged for compatibility). Route through
    // navigateTo so a forwarded `--setting` reveals the target; a
    // fragment-free string behaves exactly like setActivePage.
    m_controller->navigateTo(page);
}

} // namespace PlasmaZones
