// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsstagingdomain.h"

#include "settingscontroller.h"

#include "../core/logging.h"

#include <QDebug>
#include <QScopeGuard>

#include <memory>

namespace PlasmaZones {

SettingsStagingDomain::SettingsStagingDomain(SettingsController* controller, QObject* parent)
    : PhosphorControl::StagingDomain(parent)
    , m_controller(controller)
{
    if (!m_controller) {
        // Programmer error — a SettingsStagingDomain without a
        // controller stays permanently clean and apply/discard
        // no-op. The framework would still happily register it and
        // wire dirtyChanged through, just to a domain that can
        // never transition. Warn loudly so the bug surfaces.
        qCWarning(lcCore) << "PlasmaZones::SettingsStagingDomain: constructed with null SettingsController —"
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
    // Connection-leak prevention: the destroyed() Qt::SingleShotConnection
    // never auto-disconnects on a normal apply() (destroyed only fires
    // once at end-of-life, NOT when savingFinished fires). Without
    // explicit cleanup, every apply() that completes via
    // savingFinished would leak ONE destroyed-handler connection that
    // stays live until the controller dies. We track the destroyed
    // connection via a `Connection` captured by the savingFinished
    // lambda — the lambda explicitly disconnects it after handling
    // the result so successful applies don't accumulate connections.
    //
    // The `settled` shared flag is the back-stop for the cross-
    // connection idempotency invariant: if destroyed somehow fires
    // first (controller dies during save()'s singleShot delay), the
    // savingFinished lambda — which fires later from the queued
    // singleShot — sees `*settled == true` and bails. The
    // shared_ptr<bool> outlives BOTH lambdas until both have run or
    // been disconnected.
    //
    // On the destroyed-mid-batch defensive branch: under the current
    // ownership invariant (SettingsStagingDomain is a QObject-child
    // of m_app, m_app is a unique_ptr member of SettingsController
    // destroyed BEFORE ~SettingsController emits `destroyed()`), this
    // branch is unreachable — the staging domain is gone by the time
    // the controller's destroyed signal fires, so the connection's
    // receiver is already dead and Qt auto-disconnects. Kept as
    // documented-unreachable defence: if a future refactor inverts
    // the ownership (e.g. controller-owns-domain reversed, or domain
    // hoisted to a different parent chain), the destroyed branch
    // remains the correct cleanup path. Removing the branch would
    // make the future refactor a silent latent bug.
    //
    // Exception safety: applyGuard cleans up m_inFlight + both
    // connections if save() throws. The normal path dismisses the
    // guard before returning so cleanup runs exactly once.
    auto settled = std::make_shared<bool>(false);
    auto destroyedConn = std::make_shared<QMetaObject::Connection>();
    auto savingConn = std::make_shared<QMetaObject::Connection>();
    *savingConn = connect(
        m_controller, &SettingsController::savingFinished, this,
        [this, settled, destroyedConn]() {
            if (*settled)
                return;
            *settled = true;
            // Disconnect the still-live destroyed guard so it doesn't
            // accumulate one entry per apply() over the session.
            if (*destroyedConn)
                QObject::disconnect(*destroyedConn);
            m_inFlight = false;
            const bool ok = m_controller && !m_controller->needsSave();
            Q_EMIT applyResult(ok, ok ? QString() : QStringLiteral("One or more settings failed to save"));
        },
        Qt::SingleShotConnection);
    *destroyedConn = connect(
        m_controller, &QObject::destroyed, this,
        [this, settled, savingConn]() {
            if (*settled)
                return;
            *settled = true;
            if (*savingConn)
                QObject::disconnect(*savingConn);
            m_inFlight = false;
            Q_EMIT applyResult(false, QStringLiteral("Controller destroyed before save completed"));
        },
        Qt::SingleShotConnection);
    auto applyGuard = qScopeGuard([this, savingConn, destroyedConn]() {
        if (*savingConn)
            QObject::disconnect(*savingConn);
        if (*destroyedConn)
            QObject::disconnect(*destroyedConn);
        m_inFlight = false;
    });
    m_controller->save();
    applyGuard.dismiss();
}

void SettingsStagingDomain::discard()
{
    if (!m_controller || m_inFlight) {
        Q_EMIT discardResult(
            false, m_controller ? QStringLiteral("Discard already in flight") : QStringLiteral("No controller wired"));
        return;
    }
    m_inFlight = true;
    // Clear m_inFlight BEFORE the emit so a slot connected to
    // discardResult can re-enter apply()/discard() without tripping
    // the "already in flight" rejection. The qScopeGuard is the
    // exception-path back-stop: if load() throws, m_inFlight resets
    // anyway.
    bool loadCompleted = false;
    auto guard = qScopeGuard([this, &loadCompleted]() {
        if (!loadCompleted)
            m_inFlight = false;
    });
    m_controller->load();
    loadCompleted = true;
    m_inFlight = false;
    Q_EMIT discardResult(true, QString());
}

} // namespace PlasmaZones
