// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsstagingdomain.h"

#include "settingscontroller.h"

#include <QDebug>
#include <QScopeGuard>

#include <memory>

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
    // Hook savingFinished BEFORE save() — save() drives a singleShot(0,
    // …) inside SettingsController to drain queued daemon broadcasts,
    // and emits savingFinished from inside that singleShot. Releasing
    // m_inFlight on save() return (the prior pattern) raced the
    // daemon-broadcast drain and let a second Apply land while the
    // first was still being unwound.
    //
    // Both lambdas (savingFinished + destroyed) capture a SHARED
    // settled-flag. Without it the destroyed() Qt::SingleShotConnection
    // never auto-disconnects (destroyed only fires once, at end of
    // life), so every apply() that completes via savingFinished leaves
    // its destroyed-handler connection live until the controller dies
    // — at which point N applies fire N applyResult emissions. The
    // shared flag lets the first lambda to fire mark the apply as
    // settled and the second no-op.
    //
    // Truthful applyResult: SettingsController re-flags pages whose
    // commit failed (window-rules push, assignment flush) inside
    // save(), so post-savingFinished needsSave() tells us whether the
    // batch fully succeeded.
    auto settled = std::make_shared<bool>(false);
    connect(
        m_controller, &SettingsController::savingFinished, this,
        [this, settled]() {
            if (*settled)
                return;
            *settled = true;
            m_inFlight = false;
            const bool ok = m_controller && !m_controller->needsSave();
            Q_EMIT applyResult(ok, ok ? QString() : QStringLiteral("One or more settings failed to save"));
        },
        Qt::SingleShotConnection);
    connect(
        m_controller, &QObject::destroyed, this,
        [this, settled]() {
            if (*settled)
                return;
            *settled = true;
            m_inFlight = false;
            Q_EMIT applyResult(false, QStringLiteral("Controller destroyed before save completed"));
        },
        Qt::SingleShotConnection);
    m_controller->save();
}

void SettingsStagingDomain::discard()
{
    if (!m_controller || m_inFlight) {
        Q_EMIT discardResult(
            false, m_controller ? QStringLiteral("Discard already in flight") : QStringLiteral("No controller wired"));
        return;
    }
    m_inFlight = true;
    // RAII reset of m_inFlight — if SettingsController::load() throws
    // (some future code path), m_inFlight must NOT remain stuck true
    // or every subsequent apply/discard will be permanently refused.
    auto guard = qScopeGuard([this]() {
        m_inFlight = false;
    });
    m_controller->load();
    Q_EMIT discardResult(true, QString());
}

} // namespace PlasmaZones
