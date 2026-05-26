// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsstagingdomain.h"

#include "settingscontroller.h"

namespace PlasmaZones {

SettingsStagingDomain::SettingsStagingDomain(SettingsController* controller, QObject* parent)
    : PhosphorSettingsUi::StagingDomain(parent)
    , m_controller(controller)
{
    if (!m_controller) {
        return;
    }
    // SettingsController emits dirtyPagesChanged whenever m_dirtyPages mutates;
    // forward to the framework's StagingDomain signal so ApplicationController
    // recomputes its global dirty flag.
    connect(m_controller, &SettingsController::dirtyPagesChanged, this,
            &PhosphorSettingsUi::StagingDomain::dirtyChanged);
}

SettingsStagingDomain::~SettingsStagingDomain() = default;

bool SettingsStagingDomain::isDirty() const
{
    return m_controller && m_controller->needsSave();
}

void SettingsStagingDomain::apply()
{
    if (m_controller) {
        m_controller->save();
    }
}

void SettingsStagingDomain::discard()
{
    if (m_controller) {
        m_controller->load();
    }
}

} // namespace PlasmaZones
