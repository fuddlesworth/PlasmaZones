// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QDateTime>
#include <QLoggingCategory>
#include <QSet>

#include <algorithm>
#include <iterator>
#include <utility>

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
    bool createExpired = false;
    QStringList refusedRemovals;
    QStringList refusedCreateScreens;
    for (int i = m_ledger.size() - 1; i >= 0; --i) {
        if (m_ledger.at(i).deadline <= now) {
            qCWarning(lcWorkspaceRec) << "pending workspace op expired, kind =" << static_cast<int>(m_ledger.at(i).kind)
                                      << "desktop =" << m_ledger.at(i).desktopId;
            if (m_ledger.at(i).kind == PendingOp::Kind::Create) {
                createExpired = true;
                refusedCreateScreens.append(m_ledger.at(i).screenId);
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

    // Live cap probe: KWin refuses createDesktop past its maximum SILENTLY (a
    // void method, no desktopCreated echo), so a Create expiry with the id list
    // unchanged is the only observable form of the refusal.
    //
    // One expiry is NOT that evidence, and treating it as such was the defect:
    // a D-Bus stall, a KWin restart or a lost reply also expires a Create with
    // the list unchanged, and lowering the ceiling to the live count then
    // suspends every later create (requestCreateAt gates at equality), leaving
    // the daemon unable to produce the successful create that would disprove
    // it. A genuine refusal answers EVERY attempt the same way, so the probe
    // asks twice: the first expiry only records the episode and re-drives
    // maintenance, and only a second expiry in the same episode (the id list
    // byte-identical, so nothing landed in between) is read as the real
    // ceiling. noteCreateSucceeded() clears the evidence the moment KWin
    // answers a create at all.
    //
    // "List unchanged" is checked here rather than at request time: a create
    // that DID land grew the list past the size its op was issued against, and
    // onDesktopListSettled has already retired that op, so an entry surviving
    // to expiry is one KWin never answered.
    bool capLearned = false;
    if (createExpired && !m_lastIds.isEmpty() && m_lastIds.size() < m_desktopCap) {
        if (m_capProbeIds != m_lastIds) {
            m_capProbeIds = m_lastIds;
            m_capProbeExpiries = 1;
        } else {
            ++m_capProbeExpiries;
        }
        if (m_capProbeExpiries >= CapProbeExpiries) {
            m_desktopCap = m_lastIds.size();
            capLearned = true;
            qCWarning(lcWorkspaceRec) << "createDesktop went unanswered" << m_capProbeExpiries << "times at"
                                      << m_desktopCap << "desktops — learned as the compositor's cap";
            if (!m_capHintShown) {
                m_capHintShown = true;
                Q_EMIT capReached();
            }
        }
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

    // A Create nobody answered leaves the invariant it was repairing (a missing
    // trailing empty, a screen with no desktop) still broken, and nothing else
    // re-drives it: maintenance runs off settles and population edges, and the
    // resync below re-pulls a list that is by definition UNCHANGED, so
    // onDesktopListSettled returns early. Re-run maintenance so the request is
    // made again. That second attempt is also what gives the probe above its
    // corroborating expiry.
    //
    // Bounded by its OWN budget, not by cap-learning. Cap-learning is not a
    // terminator: the probe only concludes when the id list is byte-identical
    // across two expiries, and it is reset by any list change and by ANY
    // create landing, including one for a different screen — so on a machine
    // where this screen's creates are refused while other desktops come and
    // go, the probe never concludes and this re-drive would issue one
    // createDesktop per LedgerTimeoutMs forever. Each expiry spends one of the
    // requesting screen's MaxCreateRefusals instead; requestCreateAt refuses
    // once the budget is gone, so the re-drive below becomes a no-op for that
    // screen while a transient stall still gets its retries.
    for (const QString& screenId : std::as_const(refusedCreateScreens)) {
        const int refusals = m_createRefusals.value(screenId, 0) + 1;
        m_createRefusals.insert(screenId, refusals);
        if (refusals >= MaxCreateRefusals) {
            qCWarning(lcWorkspaceRec) << "createDesktop for" << screenId << "went unanswered" << refusals
                                      << "times — no longer re-driving it for that screen";
        }
    }
    if (createExpired && !capLearned) {
        maintainInvariants();
    }

    if (expired) {
        Q_EMIT resyncRequested();
    }
}

void WorkspaceReconciler::noteCreateSucceeded()
{
    // KWin answered a create, which is proof the ceiling is not where a run of
    // expiries suggested. Drop the accumulated probe evidence and hand back the
    // default headroom: a learned cap suspends our own creates, so without this
    // the only way out is an EXTERNAL create pushing the count past the learned
    // value (onDesktopListSettled), which a daemon whose creates are suspended
    // can never bring about itself.
    m_capProbeIds.clear();
    m_capProbeExpiries = 0;
    if (m_desktopCap < DefaultDesktopCap) {
        qCWarning(lcWorkspaceRec) << "a createDesktop landed at" << m_lastIds.size()
                                  << "desktops — dropping the learned cap of" << m_desktopCap;
        m_desktopCap = DefaultDesktopCap;
        m_capHintShown = false;
    }
}

void WorkspaceReconciler::noteCreateLandedFor(const QString& screenId)
{
    // Proof this screen's creates are being answered, so the refusal run that
    // spent its budget was a stall and not a standing refusal.
    m_createRefusals.remove(screenId);
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
    // The id comparison is the whole test: a Create carries no desktopId (KWin
    // has not told us one yet), so it can never match a non-empty id, and this
    // is only ever called with one.
    for (int i = m_ledger.size() - 1; i >= 0; --i) {
        if (m_ledger.at(i).desktopId == desktopId) {
            m_ledger.removeAt(i);
        }
    }
    if (m_ledger.isEmpty()) {
        m_ledgerTimer.stop();
    }
}

QHash<QString, WorkspaceReconciler::PendingOp> WorkspaceReconciler::takeSettledCreates(const QStringList& newIds)
{
    // Rank pairing, not nearest-distance. The ledger is oldest-request-first
    // and the settled list is KWin-position-ordered; those rankings agree only
    // when the screens happen to have asked in slice order. Screens [A,B] where
    // B's population change fires first puts Create(B) at the head of the
    // ledger while A's new desktop sits EARLIER in KWin's list, so a
    // first-in-ledger match hands each desktop to the other screen's slice —
    // and nothing corrects it, because both screens still end in a trailing
    // empty and look repaired.
    //
    // Distances cannot be compared per id either: globalPosition is captured
    // at request time and requestCreateAt does not mutate the map, so with
    // several Creates issued in one maintenance pass each recorded value
    // ignores the sibling creates that land ahead of it and is low by their
    // count. The settled indices DO include them, so a per-id nearest match
    // crosses over once that uniform drift reaches half the spacing. Ordering
    // survives the drift, so the requested positions and the new ids are
    // paired by RANK instead.
    QHash<QString, PendingOp> matched;
    if (newIds.isEmpty()) {
        return matched;
    }
    QList<int> creates;
    for (int i = 0; i < m_ledger.size(); ++i) {
        if (m_ledger.at(i).kind == PendingOp::Kind::Create) {
            creates.append(i);
        }
    }
    // Stable, so equal requested positions keep ledger (request) order.
    std::stable_sort(creates.begin(), creates.end(), [this](int a, int b) {
        return m_ledger.at(a).globalPosition < m_ledger.at(b).globalPosition;
    });
    const int pairs = qMin(creates.size(), newIds.size());
    for (int rank = 0; rank < pairs; ++rank) {
        const PendingOp& op = m_ledger.at(creates.at(rank));
        matched.insert(newIds.at(rank), op);
        // The settle is this screen's answer, exactly as an id-only echo is.
        noteCreateLandedFor(op.screenId);
    }
    if (pairs > 0) {
        noteCreateSucceeded();
    }
    QList<int> consumed = creates.mid(0, pairs);
    std::sort(consumed.begin(), consumed.end());
    for (int i = consumed.size() - 1; i >= 0; --i) {
        m_ledger.removeAt(consumed.at(i));
    }
    if (m_ledger.isEmpty()) {
        m_ledgerTimer.stop();
    }
    return matched;
}

bool WorkspaceReconciler::applySettledCreate(const PendingOp& op, const QString& desktopId)
{
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
            // KWin answered, so the cap probe's evidence is stale whichever
            // branch below realizes the desktop.
            noteCreateSucceeded();
            noteCreateLandedFor(op.screenId);
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
    // Refuse an UNTRANSLATABLE target before it reaches the ledger. The map
    // can hold an id KWin has echoed but not yet settled (onKwinDesktopCreated
    // realizes it early), and the desktop int the controller must resolve to
    // drive the effect comes from the settled list — the same list m_lastIds
    // mirrors. Ledgering such a switch is the worst of both: the request is
    // dropped downstream anyway, while the open entry short-circuits this
    // screen's foreign evaluation for the full LedgerTimeoutMs and then
    // expires into a spurious resync. The next settle makes the id live and
    // the caller (a verb, or snapBack off the next foreign report) asks again.
    if (!m_lastIds.contains(desktopId)) {
        qCWarning(lcWorkspaceRec) << "not switching" << screenId << "to" << desktopId
                                  << "- that desktop is not in the settled list yet";
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
    // Position index built once: the renumber below and the prunes further down
    // both ask about membership per entry, and a linear indexOf/contains each
    // time is quadratic in the desktop count.
    QHash<QString, int> indexOfId;
    indexOfId.reserve(ids.size());
    for (int i = 0; i < ids.size(); ++i) {
        indexOfId.insert(ids.at(i), i);
    }
    for (int oldIdx = 0; oldIdx < m_lastIds.size(); ++oldIdx) {
        const int newIdx = indexOfId.value(m_lastIds.at(oldIdx), -1);
        if (newIdx >= 0) {
            if (newIdx != oldIdx) {
                oldToNew.insert(oldIdx + 1, newIdx + 1);
            }
        } else {
            removed.append(oldIdx + 1);
        }
    }
    const int previousCount = m_lastIds.size();
    m_lastIds = ids;

    // Headroom came back: a shorter desktop list is the one event that can
    // turn a standing create refusal (KWin at its ceiling) back into a create
    // it will answer, and nothing else would ever hand the budget back — a
    // screen whose budget is spent issues no create, so it can never earn the
    // landing that clears it. The mirror of the population-change restore on
    // the removal side.
    if (m_lastIds.size() < previousCount) {
        m_createRefusals.clear();
    }

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
        it = indexOfId.contains(it.key()) ? std::next(it) : m_removalRefusals.erase(it);
    }
    for (auto it = m_namePushes.begin(); it != m_namePushes.end();) {
        it = indexOfId.contains(it.key()) ? std::next(it) : m_namePushes.erase(it);
    }
    // The census row and the removal-race latch are per-desktop bookkeeping
    // too, and neither is cleared anywhere else when a desktop disappears
    // WITHOUT a removal echo (an external destroy the daemon only ever sees as
    // a list delta), so both grew for the life of the session.
    for (auto it = m_population.begin(); it != m_population.end();) {
        it = indexOfId.contains(it.key()) ? std::next(it) : m_population.erase(it);
    }
    for (auto it = m_racedDesktops.begin(); it != m_racedDesktops.end();) {
        it = indexOfId.contains(*it) ? std::next(it) : m_racedDesktops.erase(it);
    }

    const QStringList unowned = m_map.repairAgainst(ids);
    // repairAgainst reports in KWin order, so this keeps the new ids in
    // settled-list index order — the ranking takeSettledCreates pairs against.
    QStringList newUnowned;
    for (const QString& id : unowned) {
        if (!previousIds.contains(id)) {
            newUnowned.append(id);
        }
    }
    const QHash<QString, PendingOp> settledCreates = takeSettledCreates(newUnowned);
    for (const QString& id : unowned) {
        // Creates are retired by MATCHING, never by counting. A create whose
        // id-only echo already arrived was realized (and its op consumed) in
        // onKwinDesktopCreated, so it is owned and never reaches this loop; a
        // create the settle answered first is an unowned NEW id, and it
        // consumes the open Create whose requested global position is nearest
        // this id's rank among the new ids. Counting the size delta instead
        // retired a second op per landed desktop, which left the survivor's
        // echo with no ledger entry to match (adopting onto the focused screen
        // rather than the screen that asked) and let maintainScreen request a
        // duplicate.
        const auto matched = settledCreates.constFind(id);
        if (matched != settledCreates.constEnd() && applySettledCreate(*matched, id)) {
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
    // Refusal budget spent (see MaxCreateRefusals). Checked HERE rather than
    // only at the post-expiry re-drive, because maintainScreen also runs off
    // every settle and every population edge, and each of those would put the
    // unanswered request straight back.
    //
    // A NAMED create is exempt. The budget accrues from unrelated maintenance
    // (a trailing empty this screen could not repair), and spending it must
    // not silently swallow a workspace the user just declared in settings —
    // that is a create with a visible cause and a visible absence. The
    // must-hold-one-workspace arm in maintainScreen is exempt for the same
    // class of reason and refuses itself before calling here, so what is left
    // bounded is exactly the trailing-empty repair, whose absence is
    // cosmetic and self-heals when a create lands, the count drops, or the
    // screen is re-added.
    if (name.isEmpty() && m_createRefusals.value(screenId, 0) >= MaxCreateRefusals) {
        return;
    }
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
    // Recorded so takeSettledCreates can match by position rather than by
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
        // Donor transfer covers TWO refusals, not just the cap. A spent
        // create budget means KWin is not answering this screen's requests,
        // so asking again is pointless — but leaving the screen with zero
        // workspaces breaks the invariant this whole arm exists to hold, and
        // a transfer needs no answer from KWin at all. So fall through to the
        // donor search on either refusal, and only give up when neither a
        // create nor a donor is available.
        const bool createRefused = m_createRefusals.value(screenId, 0) >= MaxCreateRefusals;
        if (m_lastIds.size() + pendingCreateCount() >= m_desktopCap || createRefused) {
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

} // namespace PhosphorWorkspaces
