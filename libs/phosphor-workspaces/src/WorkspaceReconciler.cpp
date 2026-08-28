// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QDateTime>
#include <QLoggingCategory>
#include <QSet>

#include <iterator>

Q_LOGGING_CATEGORY(lcWorkspaceRec, "plasmazones.workspaces.reconciler", QtWarningMsg)

namespace PhosphorWorkspaces {

WorkspaceReconciler::WorkspaceReconciler(QObject* parent)
    : QObject(parent)
{
    m_ledgerTimer.setInterval(LedgerTimeoutMs / 4);
    m_ledgerTimer.setSingleShot(false);
    connect(&m_ledgerTimer, &QTimer::timeout, this, &WorkspaceReconciler::expireLedger);
}

WorkspaceMap& WorkspaceReconciler::map()
{
    return m_map;
}

const WorkspaceMap& WorkspaceReconciler::map() const
{
    return m_map;
}

quint64 WorkspaceReconciler::generation() const
{
    return m_generation;
}

void WorkspaceReconciler::setDesktopCap(int cap)
{
    m_desktopCap = qMax(1, cap);
}

void WorkspaceReconciler::setFocusedScreen(const QString& screenId)
{
    if (!screenId.isEmpty()) {
        m_focusedScreen = screenId;
    }
}

void WorkspaceReconciler::bumpGeneration()
{
    ++m_generation;
    Q_EMIT mapChanged();
}

// ── Ledger ──────────────────────────────────────────────────────────────────

void WorkspaceReconciler::ledgerAdd(PendingOp op)
{
    op.deadline = QDateTime::currentMSecsSinceEpoch() + LedgerTimeoutMs;
    m_ledger.append(op);
    if (!m_ledgerTimer.isActive()) {
        m_ledgerTimer.start();
    }
}

void WorkspaceReconciler::expireLedger()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool expired = false;
    QStringList refusedRemovals;
    for (int i = m_ledger.size() - 1; i >= 0; --i) {
        if (m_ledger.at(i).deadline <= now) {
            qCWarning(lcWorkspaceRec) << "pending workspace op expired, kind =" << static_cast<int>(m_ledger.at(i).kind)
                                      << "desktop =" << m_ledger.at(i).desktopId;
            // Live cap probe: KWin refuses createDesktop past its maximum
            // SILENTLY (void method, no desktopCreated echo), so a Create
            // expiry with the list unchanged is the only observable form of
            // the refusal. Learn the ceiling from it — this replaces the
            // DefaultDesktopCap guess with the compositor's real answer.
            // Self-healing against a mislearn from a transient D-Bus stall:
            // onDesktopListSettled raises the cap back to the default the
            // moment the count ever exceeds the learned value.
            //
            // "List unchanged" is the precondition, and it is checked here
            // rather than at request time: a create that DID land grew the
            // list past the size this op was issued against, and
            // onDesktopListSettled has already retired it, so an entry that
            // survives to expiry is one KWin never answered.
            if (m_ledger.at(i).kind == PendingOp::Kind::Create && !m_lastIds.isEmpty()
                && m_lastIds.size() < m_desktopCap) {
                m_desktopCap = m_lastIds.size();
                qCWarning(lcWorkspaceRec)
                    << "createDesktop refused at" << m_desktopCap << "desktops — learned as the compositor's cap";
                if (!m_capHintShown) {
                    m_capHintShown = true;
                    Q_EMIT capReached();
                }
            }
            if (m_ledger.at(i).kind == PendingOp::Kind::Remove) {
                refusedRemovals.append(m_ledger.at(i).desktopId);
            }
            m_ledger.removeAt(i);
            expired = true;
        }
    }
    if (m_ledger.isEmpty()) {
        m_ledgerTimer.stop();
    }

    // A removal KWin refused (or lost) leaves a surplus empty desktop that
    // nothing would ever retry: the destroy timer was consumed when the Remove
    // was issued, and the population gate only fires on a fresh >0 → 0 edge
    // that will never come for a desktop that is already empty. Re-arm the
    // destroy check so maintenance gets another attempt, and re-evaluate the
    // foreign state the pending Remove had been suppressing.
    //
    // Bounded, though: a removal KWin genuinely REFUSES answers every re-arm
    // the same way, and an unbounded retry is a re-arm/re-issue/expire/resync
    // loop that never settles. After MaxRemovalRefusals rounds the surplus
    // empty desktop is left in place — visible and harmless — until its
    // population changes or it goes away, either of which restores the budget.
    for (const QString& desktopId : refusedRemovals) {
        m_racedDesktops.remove(desktopId);
        const int refusals = m_removalRefusals.value(desktopId, 0) + 1;
        m_removalRefusals.insert(desktopId, refusals);
        if (refusals >= MaxRemovalRefusals) {
            qCWarning(lcWorkspaceRec) << "removal of" << desktopId << "went unanswered" << refusals
                                      << "times — leaving the desktop in place";
            continue;
        }
        if (!m_map.ownerOf(desktopId).isEmpty() && isDesktopEmpty(desktopId)) {
            scheduleDestroyCheck(desktopId);
        }
    }
    if (!refusedRemovals.isEmpty()) {
        const QStringList screens = m_map.screenOrder();
        for (const QString& screenId : screens) {
            evaluateForeign(screenId);
        }
    }

    if (expired) {
        Q_EMIT resyncRequested();
    }
}

bool WorkspaceReconciler::hasPendingStructuralOps() const
{
    for (const auto& op : m_ledger) {
        if (op.kind != PendingOp::Kind::SetCurrent) {
            return true;
        }
    }
    return false;
}

bool WorkspaceReconciler::hasPendingRemove(const QString& desktopId) const
{
    for (const auto& op : m_ledger) {
        if (op.kind == PendingOp::Kind::Remove && op.desktopId == desktopId) {
            return true;
        }
    }
    return false;
}

bool WorkspaceReconciler::hasPendingCreate(const QString& screenId) const
{
    for (const auto& op : m_ledger) {
        if (op.kind == PendingOp::Kind::Create && op.screenId == screenId) {
            return true;
        }
    }
    return false;
}

int WorkspaceReconciler::pendingCreateCount() const
{
    int count = 0;
    for (const auto& op : m_ledger) {
        if (op.kind == PendingOp::Kind::Create) {
            ++count;
        }
    }
    return count;
}

void WorkspaceReconciler::retireLedgerFor(const QString& desktopId)
{
    for (int i = m_ledger.size() - 1; i >= 0; --i) {
        if (m_ledger.at(i).kind != PendingOp::Kind::Create && m_ledger.at(i).desktopId == desktopId) {
            m_ledger.removeAt(i);
        }
    }
    if (m_ledger.isEmpty()) {
        m_ledgerTimer.stop();
    }
}

bool WorkspaceReconciler::realizeSettledCreate(const QString& desktopId, int newIndex)
{
    // Nearest requested position wins. The ledger is oldest-request-first and
    // the settled list is KWin-position-ordered; those rankings agree only when
    // the screens happen to have asked in slice order. Screens [A,B] where B's
    // population change fires first puts Create(B) at the head of the ledger
    // while A's new desktop sits EARLIER in KWin's list, so a first-in-ledger
    // match hands each desktop to the other screen's slice — and nothing
    // corrects it, because both screens still end in a trailing empty and look
    // repaired.
    int best = -1;
    int bestDistance = 0;
    for (int i = 0; i < m_ledger.size(); ++i) {
        if (m_ledger.at(i).kind != PendingOp::Kind::Create) {
            continue;
        }
        const int distance = qAbs(m_ledger.at(i).globalPosition - newIndex);
        if (best < 0 || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    if (best < 0) {
        return false;
    }
    const PendingOp op = m_ledger.takeAt(best);
    if (m_ledger.isEmpty()) {
        m_ledgerTimer.stop();
    }
    if (!m_map.knowsScreen(op.screenId)) {
        qCWarning(lcWorkspaceRec) << "settled create" << desktopId << "whose planned screen" << op.screenId
                                  << "is gone — adopting instead";
        return false;
    }
    WorkspaceEntry entry;
    entry.desktopId = desktopId;
    entry.name = op.name;
    m_map.insert(op.screenId, op.sliceIndex, entry);
    return true;
}

// ── KWin echoes ─────────────────────────────────────────────────────────────

void WorkspaceReconciler::onKwinDesktopCreated(const QString& desktopId)
{
    if (desktopId.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_ledger.size(); ++i) {
        if (m_ledger.at(i).kind == PendingOp::Kind::Create) {
            // FIFO match: our create landed; realize the planned slice entry.
            const PendingOp op = m_ledger.takeAt(i);
            if (m_ledger.isEmpty()) {
                m_ledgerTimer.stop();
            }
            // The planned owner can have been unplugged between the request
            // and the echo. Inserting onto a screen the map no longer knows
            // would resurrect its slice (WorkspaceMap::insert re-appends an
            // unknown screen to the order), so fall back to normal adoption.
            if (!m_map.knowsScreen(op.screenId)) {
                qCWarning(lcWorkspaceRec) << "create echo for" << desktopId << "whose planned screen" << op.screenId
                                          << "is gone — adopting instead";
                if (adoptExternal(desktopId)) {
                    bumpGeneration();
                }
                return;
            }
            WorkspaceEntry entry;
            entry.desktopId = desktopId;
            entry.name = op.name;
            m_map.insert(op.screenId, op.sliceIndex, entry);
            bumpGeneration();
            return;
        }
    }
    // External creation: adopt (fork 3). The settled list reply will confirm
    // ordering; adopting here keeps repairAgainst from re-reporting it.
    if (adoptExternal(desktopId)) {
        bumpGeneration();
    }
}

void WorkspaceReconciler::onKwinDesktopRemoved(const QString& desktopId)
{
    if (desktopId.isEmpty()) {
        return;
    }
    const bool wasOurs = hasPendingRemove(desktopId);
    // Every open entry naming this desktop dies with it — a SetCurrent onto a
    // desktop that no longer exists can never be echoed, and leaving it open
    // would block that screen's next correction for the full ledger timeout
    // and then fire a spurious resync.
    retireLedgerFor(desktopId);
    m_racedDesktops.remove(desktopId);
    m_removalRefusals.remove(desktopId);
    m_namePushes.remove(desktopId);
    if (QTimer* timer = m_destroyTimers.take(desktopId)) {
        timer->stop();
        timer->deleteLater();
    }

    if (wasOurs) {
        m_map.remove(desktopId);
        m_population.remove(desktopId);
        bumpGeneration();
        return;
    }
    // External removal: KWin is the source of truth — follow, then repair
    // invariants when the list settles (slice-never-empty, trailing-empty).
    //
    // The population is dropped whatever remove() answers: the desktop is gone
    // from KWin either way, so its window count is dead bookkeeping, and
    // remove() also answers false for the silent stale-owner-row repair (a
    // case where the map really did change, just not observably). Only the
    // generation bump follows the return value, because that is the question
    // it answers — did the PUBLISHED map change.
    const bool sliceChanged = m_map.remove(desktopId);
    m_population.remove(desktopId);
    if (sliceChanged) {
        bumpGeneration();
    }
}

bool WorkspaceReconciler::onScreenDesktopReport(const QString& screenId, int desktop)
{
    // D-Bus boundary: the report comes from the effect over the wire. An empty
    // screen id or a non-positive desktop is not a state worth recording — it
    // would poison currentDesktopIdOf and the verbs resolved through it.
    if (screenId.isEmpty() || desktop < 1) {
        qCWarning(lcWorkspaceRec) << "ignoring malformed screen desktop report, screen =" << screenId
                                  << "desktop =" << desktop;
        return false;
    }

    m_currentByScreen.insert(screenId, desktop);
    const QString desktopId = (desktop <= m_lastIds.size()) ? m_lastIds.at(desktop - 1) : QString();

    // The snap-back memo is recorded for EVERY report that lands the screen on
    // one of its own desktops, echo or not: a matched SetCurrent echo is
    // exactly the case where the screen just arrived somewhere it owns, and
    // returning before this left the memo pointing at the pre-switch desktop.
    const QString owner = desktopId.isEmpty() ? QString() : m_map.ownerOf(desktopId);
    if (!desktopId.isEmpty() && owner == screenId) {
        m_lastOwnedByScreen.insert(screenId, desktopId);
    }

    for (int i = 0; i < m_ledger.size(); ++i) {
        const auto& op = m_ledger.at(i);
        if (op.kind == PendingOp::Kind::SetCurrent && op.screenId == screenId && op.desktopId == desktopId) {
            m_ledger.removeAt(i);
            if (m_ledger.isEmpty()) {
                m_ledgerTimer.stop();
            }
            return true; // echo of our own correction — no reactive policy
        }
    }

    if (!owner.isEmpty() && owner != screenId) {
        // A structural op in flight makes reports transient: KWin renumbers on
        // BOTH create and remove, and clampScreenDesktopsToCount fires interim
        // switches that read as foreign against the pre-op map. Record only;
        // onDesktopListSettled re-evaluates every screen against the settled
        // truth (plan §4.3 destroy step 5).
        if (hasPendingStructuralOps()) {
            return false;
        }
        evaluateForeign(screenId);
    }
    return false;
}

void WorkspaceReconciler::evaluateForeign(const QString& screenId)
{
    const QString desktopId = currentDesktopIdOf(screenId);
    if (desktopId.isEmpty()) {
        return;
    }
    const QString owner = m_map.ownerOf(desktopId);
    if (owner.isEmpty() || owner == screenId) {
        return;
    }
    // One correction per external event: while a SetCurrent for this screen
    // is open, further foreign reports are ignored; the open op's echo
    // retires it and the next report re-evaluates.
    for (const auto& op : m_ledger) {
        if (op.kind == PendingOp::Kind::SetCurrent && op.screenId == screenId) {
            return;
        }
    }
    Q_EMIT foreignSwitchDetected(screenId, desktopId, owner);
}

// ── Verb support ────────────────────────────────────────────────────────────

QString WorkspaceReconciler::currentDesktopIdOf(const QString& screenId) const
{
    const int current = m_currentByScreen.value(screenId, 0);
    if (current < 1 || current > m_lastIds.size()) {
        return QString();
    }
    return m_lastIds.at(current - 1);
}

QString WorkspaceReconciler::desktopIdAtOffset(const QString& screenId, int delta) const
{
    const QString currentId = currentDesktopIdOf(screenId);
    if (currentId.isEmpty()) {
        return QString();
    }
    // Resolve within the screen's OWN slice; a screen currently showing a
    // foreign desktop (pre-snap-back window) resolves nothing.
    if (m_map.ownerOf(currentId) != screenId) {
        return QString();
    }
    const int index = m_map.sliceIndexOf(currentId) + delta;
    return desktopIdAtSliceIndex(screenId, index);
}

QString WorkspaceReconciler::desktopIdAtSliceIndex(const QString& screenId, int sliceIndex) const
{
    const auto entries = m_map.slice(screenId);
    if (sliceIndex < 0 || sliceIndex >= entries.size()) {
        return QString(); // slice edge: no wrap (niri semantics)
    }
    return entries.at(sliceIndex).desktopId;
}

bool WorkspaceReconciler::issueSetCurrent(const QString& screenId, const QString& desktopId)
{
    if (screenId.isEmpty() || desktopId.isEmpty()) {
        return false;
    }
    for (const auto& op : m_ledger) {
        if (op.kind == PendingOp::Kind::SetCurrent && op.screenId == screenId) {
            return false; // one correction per screen in flight
        }
    }
    PendingOp op;
    op.kind = PendingOp::Kind::SetCurrent;
    op.screenId = screenId;
    op.desktopId = desktopId;
    ledgerAdd(op);
    Q_EMIT requestSetCurrent(screenId, desktopId);
    return true;
}

bool WorkspaceReconciler::snapBack(const QString& screenId)
{
    const QString currentId = currentDesktopIdOf(screenId);
    if (!currentId.isEmpty() && m_map.ownerOf(currentId) == screenId) {
        return false; // already home
    }
    QString target = m_lastOwnedByScreen.value(screenId);
    if (target.isEmpty() || m_map.ownerOf(target) != screenId) {
        target = desktopIdAtSliceIndex(screenId, 0);
    }
    if (target.isEmpty()) {
        return false;
    }
    return issueSetCurrent(screenId, target);
}

bool WorkspaceReconciler::reorderCurrentWorkspace(const QString& screenId, int delta)
{
    const QString currentId = currentDesktopIdOf(screenId);
    if (currentId.isEmpty() || m_map.ownerOf(currentId) != screenId) {
        return false;
    }
    const int index = m_map.sliceIndexOf(currentId);
    const int target = index + delta;
    if (target < 0 || target >= m_map.sliceSize(screenId)) {
        return false;
    }
    if (!m_map.reorderWithinSlice(currentId, target)) {
        return false;
    }
    maintainScreen(screenId); // the trailing empty may no longer be trailing
    bumpGeneration();
    return true;
}

QString WorkspaceReconciler::transferCurrentWorkspace(const QString& screenId, const QString& targetScreenId)
{
    if (targetScreenId.isEmpty() || targetScreenId == screenId || !m_map.knowsScreen(targetScreenId)) {
        return QString();
    }
    const QString currentId = currentDesktopIdOf(screenId);
    if (currentId.isEmpty() || m_map.ownerOf(currentId) != screenId) {
        return QString();
    }
    if (m_map.sliceSize(screenId) <= 1) {
        return QString(); // a screen never gives up its last desktop
    }
    if (!m_map.transfer(currentId, targetScreenId, insertIndexBeforeTrailingEmpty(targetScreenId))) {
        return QString();
    }
    // A deliberate move overrides hotplug memory: the home stamp exists so an
    // output's own workspaces come back when it is replugged, and keeping it
    // through a user transfer would yank the workspace off the screen the user
    // just put it on the next time that output reappears.
    m_map.setHomeScreen(currentId, QString());
    // The source screen must land on one of its own desktops; the target's
    // slice may need trailing-empty repair.
    snapBack(screenId);
    maintainScreen(screenId);
    maintainScreen(targetScreenId);
    bumpGeneration();
    return currentId;
}

// ── Settled list reply ──────────────────────────────────────────────────────

void WorkspaceReconciler::onDesktopListSettled(const QStringList& ids)
{
    if (ids == m_lastIds) {
        return;
    }

    // KWin never has zero desktops, so an empty settled list while we know of
    // some is a failed read upstream, not a fact. Acting on it would drop
    // every slice and have the engines reap all per-desktop state. The
    // VirtualDesktopManager already refuses to publish this, and this guard
    // keeps a second source (a resync, a test harness) from doing the damage.
    if (ids.isEmpty() && !m_lastIds.isEmpty()) {
        qCWarning(lcWorkspaceRec) << "ignoring an empty desktop list while" << m_lastIds.size()
                                  << "desktops are known — treating it as a failed read";
        Q_EMIT resyncRequested();
        return;
    }

    // Which ids this settle ADDED, captured before m_lastIds is overwritten.
    // Only a genuinely new id can be an open Create landing.
    const QSet<QString> previousIds(m_lastIds.constBegin(), m_lastIds.constEnd());

    // Renumber mapping from the id delta: ids are the fixed points.
    QHash<int, int> oldToNew;
    QList<int> removed;
    for (int oldIdx = 0; oldIdx < m_lastIds.size(); ++oldIdx) {
        const int newIdx = ids.indexOf(m_lastIds.at(oldIdx));
        if (newIdx >= 0) {
            if (newIdx != oldIdx) {
                oldToNew.insert(oldIdx + 1, newIdx + 1);
            }
        } else {
            removed.append(oldIdx + 1);
        }
    }
    m_lastIds = ids;

    // Cap self-heal: a learned (probed) ceiling below the default that the
    // live count now EXCEEDS was a mislearn (transient stall, or the
    // compositor's cap changed) — forget it. External creates past our
    // learned value are the proof the ceiling is not where we thought.
    if (m_desktopCap < DefaultDesktopCap && m_lastIds.size() > m_desktopCap) {
        qCWarning(lcWorkspaceRec) << "desktop count" << m_lastIds.size() << "exceeds the learned cap" << m_desktopCap
                                  << "— reverting to the default ceiling";
        m_desktopCap = DefaultDesktopCap;
        m_capHintShown = false;
    }

    if (!oldToNew.isEmpty() || !removed.isEmpty()) {
        Q_EMIT renumberComputed(oldToNew, removed);
    }

    // Our OWN per-screen cache is renumbered by the same mapping. It holds
    // 1-based global ints, and every resolution the verbs make
    // (currentDesktopIdOf and everything built on it) indexes m_lastIds with
    // them. Leaving them at pre-renumber values makes each screen resolve to
    // whatever desktop slid into its old slot, so the owner-wins pass below
    // and every subsequent verb would act on the wrong desktop until the
    // effect happened to re-report that output.
    if (!oldToNew.isEmpty() || !removed.isEmpty()) {
        const QSet<int> removedSet(removed.constBegin(), removed.constEnd());
        for (auto it = m_currentByScreen.begin(); it != m_currentByScreen.end();) {
            if (removedSet.contains(it.value())) {
                // The screen's desktop is gone; drop the entry rather than
                // guess. currentDesktopIdOf then resolves empty until the
                // effect reports where KWin actually put the screen, which is
                // the honest answer and what snapBack's fallback handles.
                it = m_currentByScreen.erase(it);
                continue;
            }
            it.value() = oldToNew.value(it.value(), it.value());
            ++it;
        }
    }

    // Per-desktop bookkeeping for ids KWin no longer lists has nothing left to
    // track; drop it here so neither hash can grow across a session. (A desktop
    // removed through kwinDesktopRemoved is already cleared there; this catches
    // the ones that only ever show up as a list delta.)
    for (auto it = m_removalRefusals.begin(); it != m_removalRefusals.end();) {
        it = ids.contains(it.key()) ? std::next(it) : m_removalRefusals.erase(it);
    }
    for (auto it = m_namePushes.begin(); it != m_namePushes.end();) {
        it = ids.contains(it.key()) ? std::next(it) : m_namePushes.erase(it);
    }

    const QStringList unowned = m_map.repairAgainst(ids);
    for (const QString& id : unowned) {
        // Creates are retired by MATCHING, never by counting. A create whose
        // id-only echo already arrived was realized (and its op consumed) in
        // onKwinDesktopCreated, so it is owned and never reaches this loop; a
        // create the settle answered first is an unowned NEW id, and it
        // consumes the open Create whose requested global position is nearest
        // this id's position in the settled list. Counting the size delta
        // instead retired a second op per landed desktop, which left the
        // survivor's echo with no ledger entry to match (adopting onto the
        // focused screen rather than the screen that asked) and let
        // maintainScreen request a duplicate.
        if (!previousIds.contains(id) && realizeSettledCreate(id, ids.indexOf(id))) {
            continue;
        }
        adoptExternal(id);
    }
    // Populations are NOT cleared for adopted ids: the controller's census is
    // the source of truth and a desktop that already carried windows when it
    // was adopted must keep its count (a genuinely fresh desktop simply has
    // no entry, which reads as empty).

    maintainInvariants();

    // Owner-wins re-check against the settled truth: reports that arrived
    // during a structural window (removal renumbering, clamp interim values)
    // were recorded without policy — re-evaluate every screen now.
    const QStringList screens = m_map.screenOrder();
    for (const QString& screenId : screens) {
        evaluateForeign(screenId);
    }

    // Announced unconditionally, and deliberately: a settle that only
    // REORDERED the ids leaves the map value-identical, but mapChanged is also
    // what releases the controller's quiet queue (drainQuietQueue) once the
    // structural window closes. Gating it on a map delta would strand the
    // deferred verbs of a reorder-only settle until the next unrelated change.
    // The spurious-persist half of this is already absorbed downstream, where
    // publishIfChanged diffs the serialized payload before writing.
    bumpGeneration();
}

bool WorkspaceReconciler::adoptExternal(const QString& desktopId)
{
    QString target = m_focusedScreen;
    if (target.isEmpty() || !m_map.knowsScreen(target)) {
        const QStringList order = m_map.screenOrder();
        target = order.isEmpty() ? QString() : order.first();
    }
    if (target.isEmpty()) {
        qCWarning(lcWorkspaceRec) << "no screen available to adopt desktop" << desktopId;
        return false;
    }
    // Before the screen's trailing empty (fork 3).
    WorkspaceEntry entry;
    entry.desktopId = desktopId;
    m_map.insert(target, insertIndexBeforeTrailingEmpty(target), entry);
    return true;
}

// ── Population and lifecycle ────────────────────────────────────────────────

bool WorkspaceReconciler::isDesktopEmpty(const QString& desktopId) const
{
    return m_population.value(desktopId, 0) <= 0;
}

int WorkspaceReconciler::insertIndexBeforeTrailingEmpty(const QString& screenId) const
{
    const int size = m_map.sliceSize(screenId);
    return trailingEmptyOf(screenId).isEmpty() ? size : qMax(0, size - 1);
}

QString WorkspaceReconciler::trailingEmptyOf(const QString& screenId) const
{
    const auto entries = m_map.slice(screenId);
    if (entries.isEmpty()) {
        return QString();
    }
    const WorkspaceEntry& last = entries.last();
    if (last.name.isEmpty() && isDesktopEmpty(last.desktopId)) {
        return last.desktopId;
    }
    return QString();
}

void WorkspaceReconciler::onPopulationChanged(const QString& desktopId, int windowCount)
{
    const int previous = m_population.value(desktopId, 0);
    if (previous == windowCount) {
        return;
    }
    m_population.insert(desktopId, windowCount);
    // The population is the usual reason KWin declines a removal, so any change
    // to it restores this desktop's refusal budget.
    m_removalRefusals.remove(desktopId);

    const QString owner = m_map.ownerOf(desktopId);
    if (owner.isEmpty()) {
        return;
    }

    // Destroy race (plan §4.3 step 4): a window mapped onto this desktop
    // while our removeDesktop for it is in flight. Too late to cancel — KWin
    // will sweep the window to an arbitrary neighbour when the removal lands.
    // Surface it so the controller can snapshot the census and re-route the
    // displaced windows to the owner's current workspace afterwards.
    //
    // An edge, not a level: the controller re-snapshots on every emission, so
    // further windows landing on the same doomed desktop must stay quiet. The
    // latch clears when the Remove leaves the ledger (echo, expiry, or the
    // desktop's own removal).
    if (windowCount > 0 && !m_racedDesktops.contains(desktopId) && hasPendingRemove(desktopId)) {
        m_racedDesktops.insert(desktopId);
        Q_EMIT removalRaceDetected(desktopId, owner);
    }

    if (windowCount > 0 && previous <= 0) {
        // Occupying the trailing empty appends the next one (create-on-occupy).
        const auto entries = m_map.slice(owner);
        if (!entries.isEmpty() && entries.last().desktopId == desktopId) {
            maintainScreen(owner);
        }
        return;
    }

    if (windowCount <= 0 && previous > 0) {
        scheduleDestroyCheck(desktopId);
    }
}

void WorkspaceReconciler::scheduleDestroyCheck(const QString& desktopId)
{
    // Debounce: window moves between desktops arrive as leave+arrive pairs.
    QTimer* timer = m_destroyTimers.value(desktopId);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(DestroyDebounceMs);
        m_destroyTimers.insert(desktopId, timer);
        connect(timer, &QTimer::timeout, this, [this, desktopId]() {
            if (QTimer* fired = m_destroyTimers.take(desktopId)) {
                fired->deleteLater();
            }
            // Last-moment emptiness re-check (plan §4.3 destroy step 2).
            if (!isDesktopEmpty(desktopId)) {
                return;
            }
            // Our own Remove for this desktop is already in flight: issuing a
            // second one would have KWin answer one echo and leave the other
            // to expire, which fires a spurious resync and teaches the cap
            // probe nothing true.
            if (hasPendingRemove(desktopId)) {
                return;
            }
            // Refusal budget spent (see MaxRemovalRefusals). Checked HERE and
            // not only at the re-arm because maintainScreen's surplus-empties
            // scan re-arms this same check on every settle, which would put
            // the loop straight back.
            if (m_removalRefusals.value(desktopId, 0) >= MaxRemovalRefusals) {
                return;
            }
            const QString owner = m_map.ownerOf(desktopId);
            if (owner.isEmpty()) {
                return;
            }
            const WorkspaceEntry entry = m_map.entryFor(desktopId);
            if (!entry.name.isEmpty()) {
                return; // named: destroy-exempt
            }
            const auto entries = m_map.slice(owner);
            if (entries.size() <= 1) {
                return; // a slice never becomes empty
            }
            if (entries.last().desktopId == desktopId) {
                return; // the trailing empty itself is the invariant, not surplus
            }
            PendingOp op;
            op.kind = PendingOp::Kind::Remove;
            op.desktopId = desktopId;
            ledgerAdd(op);
            Q_EMIT requestRemoveDesktop(desktopId);
        });
    }
    timer->start();
}

void WorkspaceReconciler::requestCreateAt(const QString& screenId, int sliceIndex, const QString& name)
{
    // Count the creates KWin still owes us: the id list only grows once each
    // lands, so gating on it alone lets a burst (several screens repairing
    // their trailing empty in one maintenance pass) request past the ceiling
    // and then learn a bogus cap from the refusals.
    if (m_lastIds.size() + pendingCreateCount() >= m_desktopCap) {
        if (!m_capHintShown) {
            m_capHintShown = true;
            Q_EMIT capReached();
        }
        return;
    }
    m_capHintShown = false;
    const uint globalPosition = m_map.globalPositionForInsert(screenId, sliceIndex);
    PendingOp op;
    op.kind = PendingOp::Kind::Create;
    op.screenId = screenId;
    op.sliceIndex = sliceIndex;
    // Recorded so realizeSettledCreate can match by position rather than by
    // ledger order; the two rankings disagree with concurrent Creates.
    op.globalPosition = static_cast<int>(globalPosition);
    op.name = name;
    ledgerAdd(op);
    Q_EMIT requestCreateDesktop(globalPosition, name);
}

void WorkspaceReconciler::maintainScreen(const QString& screenId)
{
    const auto entries = m_map.slice(screenId);

    // A screen must always hold at least one desktop. This one is NOT
    // cap-gated: at the ceiling a screen with an empty slice would otherwise
    // get no desktop at all and no retry, so take one from a donor screen that
    // can spare it rather than asking KWin for a desktop it will refuse.
    if (entries.isEmpty()) {
        if (hasPendingCreate(screenId)) {
            return;
        }
        if (m_lastIds.size() + pendingCreateCount() >= m_desktopCap) {
            const QStringList order = m_map.screenOrder();
            for (const QString& donor : order) {
                if (donor == screenId || m_map.sliceSize(donor) <= 1) {
                    continue;
                }
                const QString spare = trailingEmptyOf(donor);
                if (spare.isEmpty()) {
                    continue;
                }
                qCWarning(lcWorkspaceRec) << "at the desktop cap; transferring" << spare << "from" << donor << "to"
                                          << screenId << "so no screen is left without a workspace";
                m_map.transfer(spare, screenId, 0);
                maintainScreen(donor);
                return;
            }
            qCWarning(lcWorkspaceRec) << "at the desktop cap with no donor; screen" << screenId << "holds no workspace";
            return;
        }
        requestCreateAt(screenId, 0, QString());
        return;
    }

    // Trailing empty: exactly one empty dynamic desktop at the end.
    if (trailingEmptyOf(screenId).isEmpty()) {
        if (!hasPendingCreate(screenId)) {
            requestCreateAt(screenId, entries.size(), QString());
        }
        return;
    }

    // Surplus empties: destroy from the second-to-last inward, named exempt.
    // The scan does NOT stop at the first occupied desktop — an explicit
    // named-workspace position can land a create mid-slice and leave an empty
    // dynamic desktop behind it, which a stop-at-first-occupied scan would
    // never reach and which would then live forever.
    for (int i = entries.size() - 2; i >= 0; --i) {
        const WorkspaceEntry& entry = entries.at(i);
        if (entry.name.isEmpty() && isDesktopEmpty(entry.desktopId)) {
            scheduleDestroyCheck(entry.desktopId);
        }
    }
}

void WorkspaceReconciler::maintainInvariants()
{
    const QStringList order = m_map.screenOrder();
    for (const QString& screenId : order) {
        maintainScreen(screenId);
    }
}

// ── Screens ─────────────────────────────────────────────────────────────────

void WorkspaceReconciler::onScreenAdded(const QString& screenId)
{
    if (screenId.isEmpty() || m_map.knowsScreen(screenId)) {
        return;
    }
    m_map.setScreenOrder(m_map.screenOrder() << screenId);

    // Migrate displaced workspaces home (hotplug memory): every entry whose
    // homeScreenId names the returning output moves back, in its current
    // relative order, ahead of whatever trailing empty maintenance adds.
    QStringList returning;
    const QStringList owned = m_map.allDesktopIds();
    for (const QString& id : owned) {
        if (m_map.entryFor(id).homeScreenId == screenId) {
            returning.append(id);
        }
    }
    QSet<QString> touchedScreens;
    int index = 0;
    for (const QString& id : returning) {
        touchedScreens.insert(m_map.ownerOf(id));
        m_map.transfer(id, screenId, index++);
        m_map.setHomeScreen(id, QString());
    }

    maintainScreen(screenId);
    for (const QString& touched : touchedScreens) {
        maintainScreen(touched); // the foster screen's invariants re-settle
        // The foster screen may have been SHOWING one of the migrated
        // desktops; it now sits on a foreign one and needs the owner-wins
        // correction, exactly as the verb-side transfer path does.
        evaluateForeign(touched);
    }
    bumpGeneration();
}

void WorkspaceReconciler::onScreenRemoved(const QString& screenId)
{
    if (!m_map.knowsScreen(screenId)) {
        return;
    }
    const QList<WorkspaceEntry> orphaned = m_map.takeSlice(screenId);
    m_currentByScreen.remove(screenId);
    // The snap-back memo names a desktop this screen no longer owns; keeping
    // it would aim a later correction (after a replug re-adds the screen) at
    // whatever that desktop became in the meantime.
    m_lastOwnedByScreen.remove(screenId);
    // Screen-scoped ledger entries die with the screen: an open SetCurrent for
    // a gone output can never be echoed, and until it expired it would block
    // the corrections of a re-added screen with the same id and then fire a
    // spurious resync.
    for (int i = m_ledger.size() - 1; i >= 0; --i) {
        if (m_ledger.at(i).screenId == screenId) {
            m_ledger.removeAt(i);
        }
    }
    if (m_ledger.isEmpty()) {
        m_ledgerTimer.stop();
    }
    if (m_focusedScreen == screenId) {
        m_focusedScreen.clear();
    }

    QString fallback;
    const QStringList order = m_map.screenOrder();
    if (!order.isEmpty()) {
        fallback = order.first();
    }
    if (fallback.isEmpty()) {
        // Last screen went away; keep nothing (KWin keeps the desktops; the
        // next screenAdded re-adopts via list repair).
        bumpGeneration();
        return;
    }
    // Append before the fallback's trailing empty, preserving order, each
    // entry stamped with its home output for migrate-back on replug. A
    // second displacement keeps the ORIGINAL home (the entry already carries
    // one), so daisy-chained unplugs still return the workspace to where the
    // user put it.
    int index = insertIndexBeforeTrailingEmpty(fallback);
    for (auto entry : orphaned) {
        if (entry.homeScreenId.isEmpty()) {
            entry.homeScreenId = screenId;
        }
        m_map.insert(fallback, index++, entry);
    }
    maintainScreen(fallback);
    bumpGeneration();
}

void WorkspaceReconciler::onScreenOrderChanged(const QStringList& order)
{
    const QStringList before = m_map.screenOrder();
    if (order == before) {
        return;
    }
    m_map.setScreenOrder(order);
    // setScreenOrder REPAIRS the order it is handed (slice-holding screens
    // missing from it are appended), so a caller-order that differs can still
    // repair back to what we already had. Compare the result, not the request.
    if (m_map.screenOrder() == before) {
        return;
    }
    bumpGeneration();
}

// ── Adoption ────────────────────────────────────────────────────────────────

void WorkspaceReconciler::adoptAll(const QStringList& ids, const QHash<QString, QString>& currentDesktopIdByScreen)
{
    m_lastIds = ids;

    // Keep consistent restored content; repair drops vanished ids and returns
    // what still needs an owner.
    QStringList unowned = m_map.repairAgainst(ids);

    // Pass 1: each desktop currently shown by a screen goes to that screen;
    // ties resolve to the first screen in order (the map insert repairs
    // double-ownership by removal, so process in screen order and skip ids
    // already owned).
    const QStringList order = m_map.screenOrder();
    for (const QString& screenId : order) {
        const QString shownId = currentDesktopIdByScreen.value(screenId);
        if (shownId.isEmpty()) {
            continue;
        }
        // Seed the per-screen current from what the caller observed. Without
        // it every verb resolves empty until the effect's first per-output
        // report happens along, which can be a long time after adoption on a
        // session that starts on its restored desktops.
        const int globalIndex = ids.indexOf(shownId);
        if (globalIndex >= 0) {
            m_currentByScreen.insert(screenId, globalIndex + 1);
            if (m_map.ownerOf(shownId) == screenId) {
                m_lastOwnedByScreen.insert(screenId, shownId);
            }
        }
        if (unowned.contains(shownId)) {
            WorkspaceEntry entry;
            entry.desktopId = shownId;
            m_map.insert(screenId, insertIndexBeforeTrailingEmpty(screenId), entry);
            unowned.removeAll(shownId);
            m_lastOwnedByScreen.insert(screenId, shownId);
        }
    }

    // Pass 2: remaining desktops keep KWin order contiguous — each unowned id
    // joins the owner of the nearest preceding owned desktop in global order;
    // a leading run goes to the first screen.
    for (const QString& id : unowned) {
        const int globalIdx = ids.indexOf(id);
        QString target;
        for (int i = globalIdx - 1; i >= 0; --i) {
            target = m_map.ownerOf(ids.at(i));
            if (!target.isEmpty()) {
                break;
            }
        }
        if (target.isEmpty()) {
            target = order.isEmpty() ? QString() : order.first();
        }
        if (target.isEmpty()) {
            qCWarning(lcWorkspaceRec) << "adoption with no screens; desktop left unowned:" << id;
            continue;
        }
        WorkspaceEntry entry;
        entry.desktopId = id;
        m_map.insert(target, insertIndexBeforeTrailingEmpty(target), entry);
    }

    maintainInvariants();
    bumpGeneration();
}

} // namespace PhosphorWorkspaces
