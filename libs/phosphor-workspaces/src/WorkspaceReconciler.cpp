// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QDateTime>
#include <QLoggingCategory>
#include <QSet>

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
            m_ledger.removeAt(i);
            expired = true;
        }
    }
    if (m_ledger.isEmpty()) {
        m_ledgerTimer.stop();
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
    adoptExternal(desktopId);
}

void WorkspaceReconciler::onKwinDesktopRemoved(const QString& desktopId)
{
    if (desktopId.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_ledger.size(); ++i) {
        if (m_ledger.at(i).kind == PendingOp::Kind::Remove && m_ledger.at(i).desktopId == desktopId) {
            m_ledger.removeAt(i);
            m_map.remove(desktopId);
            m_population.remove(desktopId);
            bumpGeneration();
            return;
        }
    }
    // External removal: KWin is the source of truth — follow, then repair
    // invariants when the list settles (slice-never-empty, trailing-empty).
    if (m_map.remove(desktopId)) {
        m_population.remove(desktopId);
        bumpGeneration();
    }
}

bool WorkspaceReconciler::onScreenDesktopReport(const QString& screenId, int desktop)
{
    m_currentByScreen.insert(screenId, desktop);
    const QString desktopId = (desktop >= 1 && desktop <= m_lastIds.size()) ? m_lastIds.at(desktop - 1) : QString();

    for (int i = 0; i < m_ledger.size(); ++i) {
        const auto& op = m_ledger.at(i);
        if (op.kind == PendingOp::Kind::SetCurrent && op.screenId == screenId && op.desktopId == desktopId) {
            m_ledger.removeAt(i);
            return true; // echo of our own correction — no reactive policy
        }
    }

    if (!desktopId.isEmpty()) {
        const QString owner = m_map.ownerOf(desktopId);
        if (owner == screenId) {
            m_lastOwnedByScreen.insert(screenId, desktopId); // snap-back target
        }
        if (!owner.isEmpty() && owner != screenId) {
            // One correction per external event: while a SetCurrent for this
            // screen is open, further foreign reports queue (are ignored here);
            // the open op's echo retires it and the next report re-evaluates.
            for (const auto& op : m_ledger) {
                if (op.kind == PendingOp::Kind::SetCurrent && op.screenId == screenId) {
                    return false;
                }
            }
            Q_EMIT foreignSwitchDetected(screenId, desktopId, owner);
        }
    }
    return false;
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
    if (targetScreenId.isEmpty() || targetScreenId == screenId || !m_map.hasScreen(targetScreenId)) {
        return QString();
    }
    const QString currentId = currentDesktopIdOf(screenId);
    if (currentId.isEmpty() || m_map.ownerOf(currentId) != screenId) {
        return QString();
    }
    if (m_map.sliceSize(screenId) <= 1) {
        return QString(); // a screen never gives up its last desktop
    }
    int index = m_map.sliceSize(targetScreenId);
    if (!trailingEmptyOf(targetScreenId).isEmpty()) {
        index = qMax(0, index - 1);
    }
    if (!m_map.transfer(currentId, targetScreenId, index)) {
        return QString();
    }
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

    const QStringList unowned = m_map.repairAgainst(ids);
    for (const QString& id : unowned) {
        adoptExternal(id);
    }
    for (const QString& id : unowned) {
        m_population.remove(id); // fresh desktops start empty until reported
    }

    maintainInvariants();
    bumpGeneration();
}

void WorkspaceReconciler::adoptExternal(const QString& desktopId)
{
    QString target = m_focusedScreen;
    if (target.isEmpty() || !m_map.hasScreen(target)) {
        const QStringList order = m_map.screenOrder();
        target = order.isEmpty() ? QString() : order.first();
    }
    if (target.isEmpty()) {
        qCWarning(lcWorkspaceRec) << "no screen available to adopt desktop" << desktopId;
        return;
    }
    // Before the screen's trailing empty (fork 3): second-to-last slot when a
    // trailing empty exists, else append.
    int index = m_map.sliceSize(target);
    if (!trailingEmptyOf(target).isEmpty()) {
        index = qMax(0, index - 1);
    }
    WorkspaceEntry entry;
    entry.desktopId = desktopId;
    m_map.insert(target, index, entry);
}

// ── Population and lifecycle ────────────────────────────────────────────────

bool WorkspaceReconciler::isDesktopEmpty(const QString& desktopId) const
{
    return m_population.value(desktopId, 0) <= 0;
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

    const QString owner = m_map.ownerOf(desktopId);
    if (owner.isEmpty()) {
        return;
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
            m_destroyTimers.take(desktopId)->deleteLater();
            // Last-moment emptiness re-check (plan §4.3 destroy step 2).
            if (!isDesktopEmpty(desktopId)) {
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
    if (m_lastIds.size() >= m_desktopCap) {
        if (!m_capHintShown) {
            m_capHintShown = true;
            Q_EMIT capReached();
        }
        return;
    }
    m_capHintShown = false;
    PendingOp op;
    op.kind = PendingOp::Kind::Create;
    op.screenId = screenId;
    op.sliceIndex = sliceIndex;
    op.name = name;
    ledgerAdd(op);
    Q_EMIT requestCreateDesktop(m_map.globalPositionForInsert(screenId, sliceIndex), name);
}

void WorkspaceReconciler::maintainScreen(const QString& screenId)
{
    const auto entries = m_map.slice(screenId);

    // A screen must always hold at least one desktop.
    if (entries.isEmpty()) {
        bool creating = false;
        for (const auto& op : m_ledger) {
            if (op.kind == PendingOp::Kind::Create && op.screenId == screenId) {
                creating = true;
                break;
            }
        }
        if (!creating) {
            requestCreateAt(screenId, 0, QString());
        }
        return;
    }

    // Trailing empty: exactly one empty dynamic desktop at the end.
    if (trailingEmptyOf(screenId).isEmpty()) {
        bool creating = false;
        for (const auto& op : m_ledger) {
            if (op.kind == PendingOp::Kind::Create && op.screenId == screenId) {
                creating = true;
                break;
            }
        }
        if (!creating) {
            requestCreateAt(screenId, entries.size(), QString());
        }
        return;
    }

    // Surplus trailing empties (external ops can produce runs of empties at
    // the end): destroy from the second-to-last inward, named exempt.
    for (int i = entries.size() - 2; i >= 0; --i) {
        const WorkspaceEntry& entry = entries.at(i);
        if (!entry.name.isEmpty() || !isDesktopEmpty(entry.desktopId)) {
            break;
        }
        scheduleDestroyCheck(entry.desktopId);
    }
}

void WorkspaceReconciler::maintainInvariants()
{
    const QStringList order = m_map.screenOrder();
    for (const QString& screenId : order) {
        maintainScreen(screenId);
    }
}

// ── Named workspaces ────────────────────────────────────────────────────────

void WorkspaceReconciler::applyNamedWorkspaces(const QList<NamedWorkspace>& declarations, const QStringList& kwinNames)
{
    bool changed = false;

    // Pass 1: realize every declaration.
    QSet<QString> declaredNames;
    for (const NamedWorkspace& decl : declarations) {
        if (decl.name.isEmpty() || declaredNames.contains(decl.name)) {
            continue; // empty/duplicate declarations are UI-invalid; skip
        }
        declaredNames.insert(decl.name);

        // Already realized?
        QString realizedId;
        const QStringList owned = m_map.allDesktopIds();
        for (const QString& id : owned) {
            if (m_map.entryFor(id).name == decl.name) {
                realizedId = id;
                break;
            }
        }

        // Claim an unnamed desktop whose KWin name matches (restart without a
        // state file: the name we stamped last session is the identity).
        if (realizedId.isEmpty()) {
            for (int i = 0; i < m_lastIds.size() && i < kwinNames.size(); ++i) {
                const QString& id = m_lastIds.at(i);
                if (kwinNames.at(i) == decl.name && m_map.entryFor(id).name.isEmpty()) {
                    realizedId = id;
                    m_map.setName(id, decl.name);
                    changed = true;
                    break;
                }
            }
        }

        const QString pin = (!decl.outputId.isEmpty() && m_map.hasScreen(decl.outputId)) ? decl.outputId : QString();

        if (realizedId.isEmpty()) {
            // Create it. One create per declaration per pass: an open Create
            // for this name means the previous request has not settled yet.
            bool pending = false;
            for (const auto& op : m_ledger) {
                if (op.kind == PendingOp::Kind::Create && op.name == decl.name) {
                    pending = true;
                    break;
                }
            }
            if (pending) {
                continue;
            }
            QString target = pin;
            if (target.isEmpty()) {
                target = m_focusedScreen;
            }
            if (target.isEmpty() || !m_map.hasScreen(target)) {
                const QStringList order = m_map.screenOrder();
                target = order.isEmpty() ? QString() : order.first();
            }
            if (target.isEmpty()) {
                continue;
            }
            int index = decl.position >= 0 ? decl.position : m_map.sliceSize(target);
            if (decl.position < 0 && !trailingEmptyOf(target).isEmpty()) {
                index = qMax(0, index - 1);
            }
            requestCreateAt(target, index, decl.name); // cap-guarded inside
            continue;
        }

        // Pin enforcement for a realized name.
        if (!pin.isEmpty() && m_map.ownerOf(realizedId) != pin) {
            int index = decl.position >= 0 ? decl.position : m_map.sliceSize(pin);
            if (decl.position < 0 && !trailingEmptyOf(pin).isEmpty()) {
                index = qMax(0, index - 1);
            }
            m_map.transfer(realizedId, pin, index);
            changed = true;
        }
    }

    // Pass 2: names with no surviving declaration revert to dynamic.
    const QStringList owned = m_map.allDesktopIds();
    for (const QString& id : owned) {
        const QString name = m_map.entryFor(id).name;
        if (!name.isEmpty() && !declaredNames.contains(name)) {
            m_map.setName(id, QString());
            Q_EMIT requestSetDesktopName(id, QString());
            changed = true;
            if (isDesktopEmpty(id)) {
                scheduleDestroyCheck(id);
            }
        }
    }

    if (changed) {
        maintainInvariants();
        bumpGeneration();
    }
}

// ── Screens ─────────────────────────────────────────────────────────────────

void WorkspaceReconciler::onScreenAdded(const QString& screenId)
{
    if (screenId.isEmpty() || m_map.hasScreen(screenId)) {
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
    }
    bumpGeneration();
}

void WorkspaceReconciler::onScreenRemoved(const QString& screenId)
{
    if (!m_map.hasScreen(screenId)) {
        return;
    }
    const QList<WorkspaceEntry> orphaned = m_map.takeSlice(screenId);
    m_currentByScreen.remove(screenId);
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
    int index = m_map.sliceSize(fallback);
    if (!trailingEmptyOf(fallback).isEmpty()) {
        index = qMax(0, index - 1);
    }
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
    if (order == m_map.screenOrder()) {
        return;
    }
    m_map.setScreenOrder(order);
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
        if (!shownId.isEmpty() && unowned.contains(shownId)) {
            WorkspaceEntry entry;
            entry.desktopId = shownId;
            m_map.insert(screenId, m_map.sliceSize(screenId), entry);
            unowned.removeAll(shownId);
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
        m_map.insert(target, m_map.sliceSize(target), entry);
    }

    maintainInvariants();
    bumpGeneration();
}

} // namespace PhosphorWorkspaces
