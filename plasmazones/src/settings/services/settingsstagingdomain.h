// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/StagingDomain.h>
#include <QPointer>

namespace PlasmaZones {

class SettingsController;

/// PhosphorControl::StagingDomain that wraps SettingsController's existing
/// save()/load() lifecycle so the framework's UnsavedChangesFooter can
/// trigger them.
///
/// PlasmaZones tracks dirty state centrally (m_dirtyPages in SettingsController),
/// not per-page. This domain is the single source of dirty/apply/discard that
/// ApplicationController orchestrates; per-page PageAdapters report clean
/// alongside it.
class SettingsStagingDomain : public PhosphorControl::StagingDomain
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
    /// Last value of isDirty() observed by the dirtyPagesChanged
    /// forwarder lambda. Used to gate the framework-facing dirtyChanged
    /// emit so per-page transitions that do not flip the global
    /// needsSave() bool do not spuriously re-fire it.
    bool m_lastDirty = false;
    /// Re-entrancy guard for apply()/discard(). A second invocation
    /// while the controller's save()/load() is still flushing would
    /// run against stale staging state — short-circuit until the
    /// outer call returns.
    bool m_inFlight = false;
};

} // namespace PlasmaZones
