// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Shared fixtures for the WorkspaceReconciler suites. The reconciler's inputs
// are plain calls, so a "world" is just a scripted sequence of them; these are
// the two worlds every case starts from. Split into a header because the
// suites are split by concern (structural behaviour vs. the timing-driven
// ledger paths) and both need the same starting points.

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QHash>
#include <QSignalSpy>
#include <QString>
#include <QTest>

namespace WorkspaceTest {

/// A desktop id in KWin's shape: a braced UUID. WorkspaceMap::fromJson
/// validates that shape at the state-file boundary, so the fixtures speak it.
inline QString id(int n)
{
    return QStringLiteral("{00000000-0000-4000-8000-%1}").arg(n, 12, 10, QLatin1Char('0'));
}

/// Two screens, each occupied and each therefore owing a trailing-empty
/// create, with BOTH Creates open in the ledger at once. A owns {d1}
/// (occupied), B owns {d2} (occupied); nothing has landed yet.
/// `populateBFirst` flips which screen's population change fires first, and
/// so which Create heads the ledger. The screen ORDER is [A,B] either way,
/// so B-first is the case where oldest-request order and KWin-position
/// order disagree.
inline void openTwoConcurrentCreates(PhosphorWorkspaces::WorkspaceReconciler& rec, QSignalSpy& createSpy,
                                     bool populateBFirst = false)
{
    rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});
    rec.setFocusedScreen(QStringLiteral("A"));

    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    current.insert(QStringLiteral("B"), id(2));
    rec.adoptAll({id(1), id(2)}, current);
    QCOMPARE(createSpy.count(), 0);

    if (populateBFirst) {
        rec.onPopulationChanged(id(2), 1);
        rec.onPopulationChanged(id(1), 1);
    } else {
        rec.onPopulationChanged(id(1), 1);
        rec.onPopulationChanged(id(2), 1);
    }
    QCOMPARE(createSpy.count(), 2);
}

/// Adopt a two-screen world: A owns {d1} (current), B owns {d2} (current).
/// With an empty census both slices already end in an empty dynamic
/// desktop, so adoption alone requests nothing; occupying each current
/// desktop then drives the trailing-empty creates, answered so A has
/// {d1(occupied), d3(empty)} and B has {d2(occupied), d4(empty)}.
inline void adoptTwoScreens(PhosphorWorkspaces::WorkspaceReconciler& rec)
{
    rec.onScreenOrderChanged({QStringLiteral("A"), QStringLiteral("B")});
    rec.setFocusedScreen(QStringLiteral("A"));

    QSignalSpy createSpy(&rec, &PhosphorWorkspaces::WorkspaceReconciler::requestCreateDesktop);
    QHash<QString, QString> current;
    current.insert(QStringLiteral("A"), id(1));
    current.insert(QStringLiteral("B"), id(2));
    rec.adoptAll({id(1), id(2)}, current);
    QCOMPARE(createSpy.count(), 0); // both slices end in an empty desktop

    rec.onPopulationChanged(id(1), 1);
    rec.onPopulationChanged(id(2), 1);
    QCOMPARE(createSpy.count(), 2);
    // Answer them: A's lands as {d3} at global position 1, B's as {d4}.
    rec.onKwinDesktopCreated(id(3));
    rec.onKwinDesktopCreated(id(4));
    rec.onDesktopListSettled({id(1), id(3), id(2), id(4)});

    QCOMPARE(rec.map().slice(QStringLiteral("A")).size(), 2);
    QCOMPARE(rec.map().slice(QStringLiteral("B")).size(), 2);
    QCOMPARE(rec.map().ownerOf(id(3)), QStringLiteral("A"));
    QCOMPARE(rec.map().ownerOf(id(4)), QStringLiteral("B"));
}

} // namespace WorkspaceTest
