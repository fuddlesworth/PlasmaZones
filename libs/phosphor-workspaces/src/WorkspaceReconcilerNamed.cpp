// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The named-workspace half of WorkspaceReconciler: realizing each declaration
// (claim by KWin name, create, pin) and the revert-to-dynamic sweep. Split out
// of WorkspaceReconciler.cpp on file size; the state and every helper used here
// are declared in the one header, and the logging category is defined there.

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QDateTime>
#include <QLoggingCategory>
#include <QSet>

Q_DECLARE_LOGGING_CATEGORY(lcWorkspaceRec)

namespace PhosphorWorkspaces {

int WorkspaceReconciler::namedSliceIndex(const QString& screenId, int declaredPosition) const
{
    // The trailing empty is an invariant, not a slot users place things after:
    // a named workspace landing behind it makes the empty desktop mid-slice,
    // which maintenance then reaps, and the named entry inherits the trailing
    // role it is exempt from. Everything a declaration can ask for is clamped
    // to the last position BEFORE it.
    const int last = insertIndexBeforeTrailingEmpty(screenId);
    if (declaredPosition < 0) {
        return last;
    }
    return qBound(0, declaredPosition, last);
}

void WorkspaceReconciler::applyNamedWorkspaces(const QList<NamedWorkspace>& declarations, const QStringList& kwinNames)
{
    bool changed = false;

    // Pass 1: realize every declaration.
    QSet<QString> declaredNames;
    for (const NamedWorkspace& decl : declarations) {
        if (decl.name.isEmpty() || declaredNames.contains(decl.name)) {
            // Config-fed and normally rejected by the settings UI, so reaching
            // here means the config was hand-edited or a UI guard regressed —
            // worth saying out loud rather than dropping in silence.
            qCWarning(lcWorkspaceRec) << "skipping" << (decl.name.isEmpty() ? "an empty" : "a duplicate")
                                      << "named-workspace declaration:" << decl.name;
            continue;
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
                // Only a desktop the MAP owns can be claimed: setName on an
                // unowned id is a silent no-op, so claiming one would leave
                // realizedId pointing at nothing and re-run the same futile
                // claim on every apply.
                if (kwinNames.at(i) == decl.name && !m_map.ownerOf(id).isEmpty() && m_map.entryFor(id).name.isEmpty()) {
                    realizedId = id;
                    m_map.setName(id, decl.name);
                    changed = true;
                    break;
                }
            }
        }

        // Create-echo FIFO healing: our Create echo matches ledger entries
        // first-in-first-out (KWin's desktopCreated carries no position), so
        // an external creation racing ours can be realized under the declared
        // name while our real desktop adopts elsewhere. KWin's OWN name list
        // is the tiebreaker: when it disagrees about the realized entry and
        // some other desktop carries the declared name, the identity followed
        // the name — move it there.
        if (!realizedId.isEmpty()) {
            const int pos = m_lastIds.indexOf(realizedId);
            const bool kwinAgrees = pos >= 0 && pos < kwinNames.size() && kwinNames.at(pos) == decl.name;
            if (kwinAgrees) {
                // KWin's list now carries the name; whatever we pushed landed.
                m_namePushes.remove(realizedId);
            } else {
                bool moved = false;
                for (int i = 0; i < m_lastIds.size() && i < kwinNames.size(); ++i) {
                    const QString& other = m_lastIds.at(i);
                    if (other != realizedId && kwinNames.at(i) == decl.name && !m_map.ownerOf(other).isEmpty()
                        && m_map.entryFor(other).name.isEmpty()) {
                        m_map.setName(realizedId, QString());
                        m_map.setName(other, decl.name);
                        m_namePushes.remove(realizedId);
                        realizedId = other;
                        changed = true;
                        moved = true;
                        break;
                    }
                }
                // Only when `kwinNames` actually covers the id list: a caller
                // that passed none has told us nothing about KWin's side, and
                // treating that as disagreement would re-push on every apply.
                if (!moved && pos >= 0 && pos < kwinNames.size()) {
                    // Nobody else carries the name, so the map is right and
                    // KWin is behind (a rename that never reached it, or a
                    // restart that dropped it). Push it, so the KWin-name
                    // claim path can re-identify this desktop next session.
                    //
                    // Once per push window, though: this runs on every settle,
                    // and a KWin that declines the rename (or whose name list
                    // simply lags the reply) leaves kwinAgrees false, so an
                    // unguarded emit re-fires the identical rename forever. The
                    // deadline is what lets a genuinely lost push be retried.
                    const qint64 now = QDateTime::currentMSecsSinceEpoch();
                    const auto push = m_namePushes.constFind(realizedId);
                    const bool inFlight =
                        push != m_namePushes.constEnd() && push->name == decl.name && push->deadline > now;
                    if (!inFlight) {
                        m_namePushes.insert(realizedId, NamePush{decl.name, now + LedgerTimeoutMs});
                        Q_EMIT requestSetDesktopName(realizedId, decl.name);
                    }
                }
            }
        }

        const QString pin = (!decl.outputId.isEmpty() && m_map.knowsScreen(decl.outputId)) ? decl.outputId : QString();

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
            if (target.isEmpty() || !m_map.knowsScreen(target)) {
                const QStringList order = m_map.screenOrder();
                target = order.isEmpty() ? QString() : order.first();
            }
            if (target.isEmpty()) {
                continue;
            }
            requestCreateAt(target, namedSliceIndex(target, decl.position), decl.name); // cap-guarded inside
            continue;
        }

        // Pin enforcement for a realized name.
        if (!pin.isEmpty() && m_map.ownerOf(realizedId) != pin) {
            const QString previousOwner = m_map.ownerOf(realizedId);
            if (m_map.sliceSize(previousOwner) <= 1) {
                // Pinning away a screen's ONLY desktop would empty its slice,
                // and at the cap maintainScreen cannot refill it. Leave the
                // workspace where it is; the next apply retries once that
                // screen has a second desktop.
                qCWarning(lcWorkspaceRec) << "not pinning" << decl.name << "to" << pin << "yet:" << previousOwner
                                          << "would be left with no workspace";
            } else {
                m_map.transfer(realizedId, pin, namedSliceIndex(pin, decl.position));
                // A pin can move the desktop the previous owner was SHOWING.
                // That screen now sits on a foreign desktop and nothing else
                // would notice — the verbs' own transfer path snaps back for
                // exactly this reason.
                evaluateForeign(previousOwner);
                changed = true;
            }
        }
    }

    // Pass 2: names with no surviving declaration revert to dynamic.
    const QStringList owned = m_map.allDesktopIds();
    for (const QString& id : owned) {
        const QString name = m_map.entryFor(id).name;
        if (!name.isEmpty() && !declaredNames.contains(name)) {
            m_map.setName(id, QString());
            // Whatever we were pushing for this desktop is moot; the map name
            // is cleared here so this arm cannot repeat on the next apply.
            m_namePushes.remove(id);
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

} // namespace PhosphorWorkspaces
