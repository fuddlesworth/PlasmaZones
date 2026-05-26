// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorSettingsUi/StagingDomain.h>
#include <QPointer>

namespace PlasmaZones {

class SettingsController;

/// PhosphorSettingsUi::StagingDomain that wraps SettingsController's existing
/// save()/load() lifecycle so the framework's ApplyResetCancelFooter can
/// trigger them.
///
/// PlasmaZones tracks dirty state centrally (m_dirtyPages in SettingsController),
/// not per-page. This domain is the single source of dirty/apply/discard that
/// ApplicationController orchestrates; per-page PageAdapters report clean
/// alongside it.
class SettingsStagingDomain : public PhosphorSettingsUi::StagingDomain
{
    Q_OBJECT

public:
    explicit SettingsStagingDomain(SettingsController* controller, QObject* parent = nullptr);
    ~SettingsStagingDomain() override;

    bool isDirty() const override;
    void apply() override;
    void discard() override;

private:
    QPointer<SettingsController> m_controller;
};

} // namespace PlasmaZones
