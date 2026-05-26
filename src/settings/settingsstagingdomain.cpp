// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsstagingdomain.h"

#include "settingscontroller.h"

#include <QDebug>

namespace PlasmaZones {

SettingsStagingDomain::SettingsStagingDomain(SettingsController* controller, QObject* parent)
    : PhosphorSettingsUi::StagingDomain(parent)
    , m_controller(controller)
{
    if (!m_controller) {
        // Programmer error — a SettingsStagingDomain without a
        // controller stays permanently clean and apply/discard
        // no-op. The framework would still happily register it and
        // wire dirtyChanged through, just to a domain that can
        // never transition. Warn loudly so the bug surfaces.
        qWarning() << "PlasmaZones::SettingsStagingDomain: constructed with null SettingsController — "
                   << "apply/discard/isDirty will be permanently no-op.";
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
