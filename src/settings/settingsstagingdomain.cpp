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
        qWarning() << "PlasmaZones::SettingsStagingDomain: constructed with null SettingsController —"
                   << "apply/discard/isDirty will be permanently no-op.";
        return;
    }
    // SettingsController emits dirtyPagesChanged whenever m_dirtyPages
    // mutates — but the global needsSave() bool only flips between
    // empty/non-empty dirty-page sets. Forwarding dirtyPagesChanged
    // directly would re-fire StagingDomain::dirtyChanged for every
    // per-page transition, prompting ApplicationController to
    // recompute its global dirty flag and re-render the footer for
    // changes the global flag did not actually see.
    m_lastDirty = isDirty();
    connect(m_controller, &SettingsController::dirtyPagesChanged, this, [this]() {
        const bool currentDirty = isDirty();
        if (currentDirty == m_lastDirty) {
            return;
        }
        m_lastDirty = currentDirty;
        Q_EMIT dirtyChanged();
    });
}

SettingsStagingDomain::~SettingsStagingDomain() = default;

bool SettingsStagingDomain::isDirty() const
{
    return m_controller && m_controller->needsSave();
}

void SettingsStagingDomain::apply()
{
    if (!m_controller || m_inFlight) {
        // m_inFlight short-circuits a re-entrant Apply (e.g. user clicks
        // Apply twice during the singleShot reset window of save()'s
        // m_saving flag). The second save would run against stale staging
        // state still mid-flush by the first.
        //
        // Synchronously emit applyResult so the framework's async
        // completion tracker (ApplicationController::applyAllAsync)
        // doesn't wait forever on a domain that refused the call.
        Q_EMIT applyResult(
            false, m_controller ? QStringLiteral("Apply already in flight") : QStringLiteral("No controller wired"));
        return;
    }
    m_inFlight = true;
    m_controller->save();
    m_inFlight = false;
    // SettingsController::save() is fully synchronous (KConfig write
    // to disk via QSettings). Emit completion immediately so
    // applyAllAsync's per-domain wait-counter ticks down.
    Q_EMIT applyResult(true, QString());
}

void SettingsStagingDomain::discard()
{
    if (!m_controller || m_inFlight) {
        Q_EMIT discardResult(
            false, m_controller ? QStringLiteral("Discard already in flight") : QStringLiteral("No controller wired"));
        return;
    }
    m_inFlight = true;
    m_controller->load();
    m_inFlight = false;
    Q_EMIT discardResult(true, QString());
}

} // namespace PlasmaZones
