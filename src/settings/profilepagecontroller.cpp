// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "profilepagecontroller.h"

#include "../config/configdefaults.h"
#include "../config/configmigration.h"
#include "../config/settings.h"
#include "../phosphor_i18n.h"
#include "profilestore.h"
#include "rulecontroller.h"
#include "rulemodel.h"

namespace PlasmaZones {

ProfilePageController::ProfilePageController(Settings& settings, RuleController& rules, QObject* parent)
    : PhosphorControl::PageController(QStringLiteral("profiles"), parent)
    , m_settings(settings)
    , m_rules(rules)
{
    ProfileStore::Config config;
    config.profilesDir = []() {
        return ConfigDefaults::profilesDir();
    };
    config.currentConfig = [this]() {
        return m_settings.exportConfigToJson();
    };
    config.defaultConfig = [this]() {
        return m_settings.defaultConfigJson();
    };
    config.applyConfig = [this](const QJsonObject& blob) {
        // Stage the resolved config into the settings store (uncommitted). The
        // owning config pages badge dirty value-based and the global Save
        // commits it; no beginExternalEdit envelope is needed because every
        // key applied here is a schema-backed, value-based manifest key.
        return m_settings.applyConfigOverlayStaged(blob);
    };
    config.stagedActiveId = [this]() {
        return m_stagedActiveId;
    };
    config.setStagedActiveId = [this](const QString& id) {
        updateStagedActive(id);
    };
    config.currentUserRules = [this]() {
        // The live non-managed user rules from the Rules page's staged model.
        QList<PhosphorRules::Rule> out;
        for (const PhosphorRules::Rule& r : m_rules.model()->rules()) {
            if (!r.managed)
                out.append(r);
        }
        return out;
    };
    config.applyUserRules = [this](const QList<PhosphorRules::Rule>& userRules) {
        // Stage into the Rules page model; the global Save commits over D-Bus.
        m_rules.stageUserRules(userRules);
    };
    config.ruleTitle = [this](const PhosphorRules::Rule& rule) {
        // Same title the Rules page renders, so an unnamed rule reads as its
        // match summary in the profile diff too.
        return m_rules.model()->titleFor(rule);
    };
    config.formatVersion = ConfigSchemaVersion;

    m_store = new ProfileStore(std::move(config), this);

    // Seed both pointers from the committed value on disk so the page starts clean.
    m_committedActiveId = m_store->committedActiveId();
    m_stagedActiveId = m_committedActiveId;

    // Track every committed-pointer write, including the ones the store makes
    // on its own (removeProfile clears the pointer when the active profile is
    // deleted). Without this the cached copy goes stale: the page reads
    // spuriously dirty and a Discard would resurrect the deleted id as the
    // staged pointer.
    connect(m_store, &ProfileStore::committedActiveIdChanged, this, [this](const QString& id) {
        if (m_committedActiveId == id) {
            return;
        }
        const bool wasDirty = isDirty();
        m_committedActiveId = id;
        if (isDirty() != wasDirty) {
            Q_EMIT dirtyChanged();
        }
    });
}

void ProfilePageController::updateStagedActive(const QString& id)
{
    if (m_stagedActiveId == id) {
        return;
    }
    const bool wasDirty = isDirty();
    m_stagedActiveId = id;
    if (isDirty() != wasDirty) {
        Q_EMIT dirtyChanged();
    }
}

void ProfilePageController::apply()
{
    if (m_stagedActiveId != m_committedActiveId) {
        if (!m_store->writeActiveId(m_stagedActiveId)) {
            // Keep the staged pointer (and the dirty state) so Save can be
            // retried instead of reporting a success that never hit disk.
            Q_EMIT applyResult(false, PhosphorI18n::tr("Could not save the active profile selection."));
            return;
        }
        // m_committedActiveId and dirtyChanged are handled by the
        // committedActiveIdChanged connection writeActiveId just fired.
    }
    // Synchronous domain — the framework's async batch driver waits on this.
    Q_EMIT applyResult(true, QString());
}

void ProfilePageController::discard()
{
    if (m_stagedActiveId != m_committedActiveId) {
        m_stagedActiveId = m_committedActiveId;
        Q_EMIT dirtyChanged();
        // The staged config itself reverts through the global Settings reload;
        // this only reverts the active pointer. Refresh the list badge.
        m_store->refresh();
    }
    Q_EMIT discardResult(true, QString());
}

} // namespace PlasmaZones
