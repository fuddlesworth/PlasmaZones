// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorControl/PageController.h>

#include "settings/stores/profilestore.h"

#include <QObject>
#include <QString>

namespace PlasmaZones {

class Settings;
class RuleController;

/// Page controller for the "Profiles" settings page (id "profiles").
///
/// Owns the ProfileStore (handed to QML as the `bridge` property, so bindings
/// read `settingsController.profilesPage.bridge.availableProfiles()` etc.) and
/// bridges the profile feature into the Save-footer lifecycle.
///
/// The store writes profile FILES immediately (create / rename / delete /
/// duplicate / import / export / reparent). Only two things follow the footer:
///   * the applied config — activation stages it via
///     Settings::applyConfigOverlayStaged, so the owning config pages badge
///     dirty (value-based) and the global Save commits it through Settings;
///   * the active-profile pointer — this controller holds the STAGED active id
///     and reports itself dirty while it differs from the committed pointer in
///     index.json. apply() writes index.json; discard() reverts to it (the
///     config itself reverts through the global Settings reload).
class ProfilePageController : public PhosphorControl::PageController
{
    Q_OBJECT
    Q_PROPERTY(PlasmaZones::ProfileStore* bridge READ bridge CONSTANT)

public:
    /// @p settings and @p rules are required for every config/rule diff/apply
    /// the store performs, so they are taken by reference (a compile-time
    /// non-null guarantee).
    ProfilePageController(Settings& settings, RuleController& rules, QObject* parent = nullptr);

    ProfileStore* bridge() const
    {
        return m_store;
    }

    // ── StagingDomain contract (the active-profile pointer) ──────────────────
    bool isDirty() const override
    {
        return m_stagedActiveId != m_committedActiveId;
    }
    void apply() override;
    void discard() override;

private:
    /// Set the staged active id, refresh the list badge, and recompute dirty.
    void updateStagedActive(const QString& id);

    Settings& m_settings;
    RuleController& m_rules;
    ProfileStore* m_store = nullptr;
    QString m_committedActiveId;
    QString m_stagedActiveId;
};

} // namespace PlasmaZones
