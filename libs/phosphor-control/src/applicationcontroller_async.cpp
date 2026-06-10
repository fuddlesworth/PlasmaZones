// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Async state-machine half of ApplicationController:
//   * applyAllAsync / discardAllAsync — collect per-domain terminal
//     signals, drive applyingChanged / discardingChanged transitions,
//     emit applyAllComplete / discardAllComplete with the aggregate
//     ok/errors.
//   * completeApplyIfDone / completeDiscardIfDone — terminal-emit
//     helpers shared by the result/destroyed/timeout entry points.
//   * forceResetAsyncState — emergency escape hatch when both result
//     and destroyed() failed to fire.
//   * asyncBatchTimeoutMs / setAsyncBatchTimeoutMs — consumer-facing
//     timeout tuning.
//
// Split out of applicationcontroller.cpp so that TU stays under the
// project's 800-line cap (CLAUDE.md). All methods here are members of
// PhosphorControl::ApplicationController and only touch private
// members declared in the public header — same class, separate TU,
// no API change.

#include "PhosphorControl/ApplicationController.h"

#include "PhosphorControl/StagingDomain.h"

#include <QDebug>
#include <QTimer>

namespace PhosphorControl {

void ApplicationController::applyAllAsync()
{
    // Already in flight — second click is a no-op rather than starting
    // a parallel batch. UnsavedChangesFooter's Save button is gated on
    // !applying so a user shouldn't reach this in practice, but the
    // guard keeps the contract clean for QML callers that bypass the
    // chrome.
    if (m_applying || m_discarding) {
        // Same mutual-exclusion rationale as the sync paths — both
        // async batches share m_inTransaction and per-domain
        // outstanding sets; running them concurrently would corrupt
        // either book.
        return;
    }
    // Count dirty domains and snapshot the iteration target to survive
    // any synchronous m_domains mutation during apply (same rationale
    // as applyAll above). The working list holds QPointers, not raw
    // pointers, so a domain destroyed mid-batch (between pass-1
    // collection and pass-2 dispatch) becomes a null QPointer and is
    // skipped rather than dereferenced.
    const QList<QPointer<StagingDomain>> snapshot = m_domains;
    QList<QPointer<StagingDomain>> dirty;
    dirty.reserve(snapshot.size());
    for (const auto& domain : snapshot) {
        if (domain && domain->isDirty())
            dirty.append(domain);
    }
    if (dirty.isEmpty()) {
        // Nothing to do — drive applyingChanged through the full
        // false→true→false transition (matches the dirty-domain
        // path's observable contract) so consumers binding "show
        // toast on applyingChanged false→true" don't silently miss
        // the no-op batch.
        //
        // Also bump the generation + clear error/outstanding state so a
        // prior forceResetAsyncState that left stragglers behind can't
        // leak into the next real batch's observable state.
        ++m_applyGeneration;
        m_applyErrors.clear();
        m_applyOutstanding.clear();
        m_applying = true;
        Q_EMIT applyingChanged();
        m_applying = false;
        Q_EMIT applyingChanged();
        Q_EMIT applyAllComplete(true, QStringList{});
        return;
    }

    m_applyErrors.clear();
    m_applyOutstanding.clear();
    m_applyOutstanding.reserve(dirty.size());
    for (const auto& dp : dirty) {
        if (auto* d = dp.data())
            m_applyOutstanding.insert(d);
    }
    // Set pending counter from the outstanding set, not from the
    // input list — duplicate StagingDomain pointers in `dirty`
    // (defence-in-depth against trackDomain's contains-check) would
    // otherwise leave the counter > set size and the batch stuck
    // permanently mid-flight.
    m_applyPending = m_applyOutstanding.size();
    // Bump the generation BEFORE flipping m_applying / emitting
    // applyingChanged so any slot that reads m_applyGeneration from
    // the change handler sees the new value, not the previous batch's.
    ++m_applyGeneration;
    m_applying = true;
    // Set m_inTransaction BEFORE applyingChanged emits so any slot
    // that triggers domain dirtyChanged in response (cascade) takes
    // the m_inTransaction-guarded path inside onDomainDirtyChanged
    // instead of running a full O(N) recomputeDirty walk (the A15
    // batching optimisation).
    m_inTransaction = true;
    Q_EMIT applyingChanged();

    // Hard timeout — if any pending domain hasn't reported by the
    // deadline, synthesise a failure entry per pending domain, zero
    // the counter, and emit applyAllComplete(false, …). Without this
    // a stuck D-Bus reply with no client-side timeout would freeze
    // the chrome's "Saving…" indicator indefinitely.
    //
    // Captures the generation counter so a stale timer from a
    // previous batch can't mistakenly fire against a new batch the
    // user kicked off after the first one already completed.
    const quint64 generation = m_applyGeneration;
    QTimer::singleShot(m_asyncBatchTimeoutMs, this, [this, generation]() {
        if (generation != m_applyGeneration)
            return;
        if (!m_applying || m_applyPending == 0)
            return;
        // Append one error per stuck domain, naming each by its
        // objectName when set (consumers can stamp this in their
        // constructor) so the chrome's toast can list which page
        // wedged instead of N identical strings.
        for (StagingDomain* stuck : m_applyOutstanding) {
            const QString name = stuck ? stuck->objectName() : QString();
            m_applyErrors.append(
                name.isEmpty() ? QStringLiteral("Domain did not report apply completion within timeout")
                               : QStringLiteral("Domain %1 did not report apply completion within timeout").arg(name));
        }
        m_applyPending = 0;
        completeApplyIfDone();
    });

    // Per-domain lambdas share the same `generation` snapshot captured
    // above for the timer — see the generation-counter comment in the
    // header. Reusing the variable keeps the timer + per-domain
    // lambdas semantically equivalent for stale-fire detection.
    //
    // Two-pass: connect ALL per-domain terminal handlers BEFORE
    // calling any `apply()`. If domain N's synchronous apply() were
    // to cascade and trigger domain N+1's `applyResult` (e.g. a
    // shared signal bus, a cascading config-write), the single-pass
    // form would lose that emission because N+1's lambda hadn't been
    // connected yet — m_applyPending then never reaches 0 and the
    // 60 s timer is the only recovery. Two-pass closes that hole.
    //
    // Track every per-domain connection so completeApplyIfDone can
    // disconnect the lot at terminal emit. Qt::SingleShotConnection
    // self-disconnects only when the signal actually fires —  on
    // domains that completed during this batch but never re-participate
    // in subsequent batches, the destroyed() lambda would otherwise stay
    // wired forever, accumulating one dead lambda per past batch on
    // every long-lived domain.
    m_applyConnections.clear();
    m_applyConnections.reserve(dirty.size() * 2);
    for (const auto& dp : dirty) {
        auto* domain = dp.data();
        if (!domain)
            continue;
        // The lambda checks generation FIRST (stale-batch guard) and
        // then removes the domain from m_applyOutstanding — if the
        // domain isn't in the set, this is the SECOND terminal fire
        // (applyResult ran sync then destroyed() fired, or vice-
        // versa) and we no-op so the pending counter only ticks once
        // per domain regardless of how many terminal signals arrive.
        m_applyConnections.append(connect(domain, &StagingDomain::applyResult, this,
                                          [this, domain, generation](bool ok, const QString& error) {
                                              if (generation != m_applyGeneration)
                                                  return;
                                              if (m_applyOutstanding.remove(domain) == 0)
                                                  return;
                                              if (!ok && !error.isEmpty())
                                                  m_applyErrors.append(error);
                                              else if (!ok)
                                                  m_applyErrors.append(QStringLiteral("Unknown error"));
                                              --m_applyPending;
                                              completeApplyIfDone();
                                          }));
        // Companion guard for the destroyed-mid-batch case: if the
        // domain QObject dies before it emits applyResult, this fires
        // and ticks the counter so m_applying doesn't stall true.
        // Shares the generation + outstanding-set protocol with the
        // applyResult lambda above so the two are idempotent.
        m_applyConnections.append(connect(domain, &QObject::destroyed, this, [this, domain, generation]() {
            if (generation != m_applyGeneration)
                return;
            if (m_applyOutstanding.remove(domain) == 0)
                return;
            m_applyErrors.append(QStringLiteral("Domain destroyed before apply completed"));
            --m_applyPending;
            completeApplyIfDone();
        }));
    }
    // Second pass — every lambda is wired, now drive the apply()s.
    // Iterate via QPointer so a domain destroyed between pass-1 and
    // pass-2 becomes a null QPointer and is skipped rather than
    // dereferenced.
    for (const auto& dp : dirty) {
        if (auto* domain = dp.data())
            domain->apply();
    }
}

void ApplicationController::discardAllAsync()
{
    if (m_discarding || m_applying) {
        // Symmetric to applyAllAsync — see comment there.
        return;
    }
    const QList<QPointer<StagingDomain>> snapshot = m_domains;
    QList<QPointer<StagingDomain>> dirty;
    dirty.reserve(snapshot.size());
    for (const auto& domain : snapshot) {
        if (domain && domain->isDirty())
            dirty.append(domain);
    }
    if (dirty.isEmpty()) {
        // Drive discardingChanged through the full false→true→false
        // transition (matches the dirty-domain path's contract).
        // Also bump the generation + clear error/outstanding state so
        // a prior forceResetAsyncState that left stragglers behind
        // can't leak into the next real batch's observable state
        // (symmetric with applyAllAsync no-op fast path).
        ++m_discardGeneration;
        m_discardErrors.clear();
        m_discardOutstanding.clear();
        m_discarding = true;
        Q_EMIT discardingChanged();
        m_discarding = false;
        Q_EMIT discardingChanged();
        Q_EMIT discardAllComplete(true, QStringList{});
        return;
    }

    m_discardErrors.clear();
    m_discardOutstanding.clear();
    m_discardOutstanding.reserve(dirty.size());
    for (const auto& dp : dirty) {
        if (auto* d = dp.data())
            m_discardOutstanding.insert(d);
    }
    // See applyAllAsync rationale for the from-set sizing.
    m_discardPending = m_discardOutstanding.size();
    ++m_discardGeneration;
    m_discarding = true;
    m_inTransaction = true;
    Q_EMIT discardingChanged();

    // Same hard-timeout safety net as applyAllAsync — see comment
    // there for rationale. Generation capture protects against a
    // stale timer firing against a subsequent batch.
    const quint64 generation = m_discardGeneration;
    QTimer::singleShot(m_asyncBatchTimeoutMs, this, [this, generation]() {
        if (generation != m_discardGeneration)
            return;
        if (!m_discarding || m_discardPending == 0)
            return;
        // Same per-domain naming as the apply path so the chrome can
        // identify the wedged page(s).
        for (StagingDomain* stuck : m_discardOutstanding) {
            const QString name = stuck ? stuck->objectName() : QString();
            m_discardErrors.append(
                name.isEmpty()
                    ? QStringLiteral("Domain did not report discard completion within timeout")
                    : QStringLiteral("Domain %1 did not report discard completion within timeout").arg(name));
        }
        m_discardPending = 0;
        completeDiscardIfDone();
    });

    // Per-domain lambdas reuse the same `generation` snapshot above
    // (timer + apply/destroyed lambdas all key off it for stale-fire
    // detection). Two-pass connect-then-discard mirrors the
    // applyAllAsync structure so cross-domain sync cascades don't
    // lose terminal signals.
    //
    // Track every per-domain connection so completeDiscardIfDone can
    // disconnect them at terminal emit — matches the applyAllAsync
    // rationale above (Qt::SingleShotConnection alone leaks
    // destroyed() lambdas onto domains that never re-participate).
    m_discardConnections.clear();
    m_discardConnections.reserve(dirty.size() * 2);
    for (const auto& dp : dirty) {
        auto* domain = dp.data();
        if (!domain)
            continue;
        // Same generation + outstanding-set guard pattern as
        // applyAllAsync above. See the rationale comment there.
        m_discardConnections.append(connect(domain, &StagingDomain::discardResult, this,
                                            [this, domain, generation](bool ok, const QString& error) {
                                                if (generation != m_discardGeneration)
                                                    return;
                                                if (m_discardOutstanding.remove(domain) == 0)
                                                    return;
                                                if (!ok && !error.isEmpty())
                                                    m_discardErrors.append(error);
                                                else if (!ok)
                                                    m_discardErrors.append(QStringLiteral("Unknown error"));
                                                --m_discardPending;
                                                completeDiscardIfDone();
                                            }));
        m_discardConnections.append(connect(domain, &QObject::destroyed, this, [this, domain, generation]() {
            if (generation != m_discardGeneration)
                return;
            if (m_discardOutstanding.remove(domain) == 0)
                return;
            m_discardErrors.append(QStringLiteral("Domain destroyed before discard completed"));
            --m_discardPending;
            completeDiscardIfDone();
        }));
    }
    // Second pass — every lambda is wired, now drive the discard()s.
    // Iterate via QPointer so a domain destroyed between pass-1 and
    // pass-2 becomes a null QPointer and is skipped rather than
    // dereferenced.
    for (const auto& dp : dirty) {
        if (auto* domain = dp.data())
            domain->discard();
    }
}

void ApplicationController::completeApplyIfDone()
{
    if (m_applyPending > 0)
        return;
    m_applyOutstanding.clear();
    // Disconnect every per-domain lambda wired by applyAllAsync. Without
    // this, a domain that participated in this batch but won't appear in
    // any subsequent batch would still carry a dead applyResult /
    // destroyed lambda on its slot list. Cheap (O(N) for the batch's N
    // dirty domains) and runs once per terminal completion.
    for (const auto& c : m_applyConnections) {
        QObject::disconnect(c);
    }
    m_applyConnections.clear();
    // State-change emission order:
    //   1) m_applying = false        (set field first so probes see it)
    //   2) m_inTransaction = false   (set AFTER m_applying so a slot fired
    //                                 by the terminal emit observes the
    //                                 intended (applying=false,
    //                                 inTransaction=false) pair)
    //   3) applyingChanged           (state-bit observers refresh)
    //   4) applyAllComplete          (terminal-batch signal fires while
    //                                 applying is still observably false;
    //                                 a slot that re-invokes applyAllAsync
    //                                 from here starts a fresh batch
    //                                 cleanly)
    //   5) recomputeDirty            (dirtyChanged-driven side effects
    //                                 can now observe both applying=false
    //                                 AND the terminal signal has already
    //                                 been emitted — no chance of
    //                                 Complete arriving AFTER a re-invoked
    //                                 batch's applyingChanged(true))
    const bool ok = m_applyErrors.isEmpty();
    const QStringList errors = std::move(m_applyErrors);
    // Defensive: std::move leaves the source in a valid-but-
    // unspecified state. The next applyAllAsync also clears, but a
    // re-entrant applyAllComplete listener that reads m_applyErrors
    // would otherwise see whatever the QStringList's move-from
    // implementation left behind.
    m_applyErrors.clear();
    m_applying = false;
    m_inTransaction = false;
    Q_EMIT applyingChanged();
    Q_EMIT applyAllComplete(ok, errors);
    recomputeDirty();
}

void ApplicationController::completeDiscardIfDone()
{
    if (m_discardPending > 0)
        return;
    m_discardOutstanding.clear();
    // Disconnect every per-domain lambda wired by discardAllAsync —
    // see completeApplyIfDone for rationale.
    for (const auto& c : m_discardConnections) {
        QObject::disconnect(c);
    }
    m_discardConnections.clear();
    // Same emission order as completeApplyIfDone — m_discarding flips
    // false BEFORE m_inTransaction so a slot triggered by the terminal
    // emit observes the intended pair. See the comment block there.
    const bool ok = m_discardErrors.isEmpty();
    const QStringList errors = std::move(m_discardErrors);
    // Defensive clear after std::move (same rationale as
    // completeApplyIfDone — see comment there).
    m_discardErrors.clear();
    m_discarding = false;
    m_inTransaction = false;
    Q_EMIT discardingChanged();
    Q_EMIT discardAllComplete(ok, errors);
    recomputeDirty();
}

void ApplicationController::forceResetAsyncState()
{
    // Emergency escape hatch — recovers from a state machine that's
    // stuck because both the terminal result signal AND the
    // destroyed() guard failed to fire (should be unreachable but
    // chrome buttons would be permanently disabled if it did).
    //
    // Bumping the generation counter is essential: the per-domain
    // applyResult/destroyed lambdas from the wedged batch are STILL
    // WIRED on the domains. If those domains later fire either signal
    // after this reset, the lambdas' generation guard will see the
    // new value and bail — without the bump, a stale fire after
    // reset would corrupt whatever state-machine state happens to be
    // live (the NEXT batch's counters, most likely).
    // Mutually-exclusive `if / else if` rather than two sequential
    // if-blocks: applyAllAsync/discardAllAsync upstream guards forbid
    // both flags being true concurrently, so in a healthy state at
    // most one branch can fire. If a future caller bypassed those
    // guards (or a regression let both flags survive), running BOTH
    // branches in one call would have applyOnly clear m_inTransaction
    // before discardOnly inherited the same shared flag — cascading
    // recomputeDirty walks where the documented contract is "one
    // recompute per terminal batch". Recover one wedge per call;
    // callers wanting both reset run forceResetAsyncState twice.
    if (m_applying) {
        ++m_applyGeneration;
        m_applyPending = 0;
        m_applyOutstanding.clear();
        if (m_applyErrors.isEmpty())
            m_applyErrors.append(QStringLiteral("Async apply state force-reset"));
        completeApplyIfDone();
    } else if (m_discarding) {
        ++m_discardGeneration;
        m_discardPending = 0;
        m_discardOutstanding.clear();
        if (m_discardErrors.isEmpty())
            m_discardErrors.append(QStringLiteral("Async discard state force-reset"));
        completeDiscardIfDone();
    }
}

int ApplicationController::asyncBatchTimeoutMs() const
{
    return m_asyncBatchTimeoutMs;
}

void ApplicationController::setAsyncBatchTimeoutMs(int ms)
{
    if (ms <= 0) {
        qWarning() << "ApplicationController::setAsyncBatchTimeoutMs: refusing non-positive value" << ms
                   << "— keeping current" << m_asyncBatchTimeoutMs;
        return;
    }
    m_asyncBatchTimeoutMs = ms;
}

} // namespace PhosphorControl
