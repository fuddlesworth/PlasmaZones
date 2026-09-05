// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QSignalSpy>
#include <QTest>

#include "helpers/AutotileTestHelpers.h"
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorTileEngine/AutotileEngine.h>

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

/**
 * @brief Autotile dynamic-workspaces arms: identity-based reap must tear the
 *        dead desktop's state down through the full release path (its windows
 *        are alive — KWin relocates them), and renumber must shift every
 *        per-desktop key so no stale pre-shift int survives (the mutation the
 *        shared re-key pass exists to make impossible).
 */
class TestAutotileEngineWorkspaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void reapDesktopState_dropsOnlyTheDeadDesktop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        PhosphorEngine::WindowRegistry registry;
        engine.setWindowRegistry(&registry);
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("app|d1"), screen);
        engine.setCurrentDesktopForScreen(screen, 2);
        engine.windowOpened(QStringLiteral("app|d2"), screen);
        QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{1, 2}));

        engine.reapDesktopState(1);
        QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{2}));
    }

    void renumberDesktopState_shiftsEveryKey()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        PhosphorEngine::WindowRegistry registry;
        engine.setWindowRegistry(&registry);
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        engine.setCurrentDesktopForScreen(screen, 3);
        engine.windowOpened(QStringLiteral("app|d3"), screen);
        engine.setCurrentDesktopForScreen(screen, 4);
        engine.windowOpened(QStringLiteral("app|d4"), screen);
        // setAutotileScreens seeded an (empty) desktop-1 state too.
        QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{1, 3, 4}));

        // Desktop 2 died: 3→2, 4→3; 1 is absent from the mapping and stays.
        // Mutation guard: no key keeps a pre-shift int, and the tracker moved
        // with the states so the current context (old 4 = new 3) still
        // resolves its window.
        QHash<int, int> mapping;
        mapping.insert(3, 2);
        mapping.insert(4, 3);
        engine.renumberDesktopState(mapping);
        QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{1, 2, 3}));
        QVERIFY(engine.isWindowTracked(QStringLiteral("app|d4")));
    }

    // Reap and renumber fire continuously once workspaces are dynamic, so the
    // single-invocation cases above are not enough: a second reap of an
    // already-dead index must be inert, and a renumber that follows a reap
    // must act on what the reap left rather than on the pre-reap key set.
    void reapThenRenumber_repeatsWithoutResurrectingState()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        PhosphorEngine::WindowRegistry registry;
        engine.setWindowRegistry(&registry);
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        engine.windowOpened(QStringLiteral("app|d1"), screen);
        engine.setCurrentDesktopForScreen(screen, 2);
        engine.windowOpened(QStringLiteral("app|d2"), screen);
        engine.setCurrentDesktopForScreen(screen, 3);
        engine.windowOpened(QStringLiteral("app|d3"), screen);
        QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{1, 2, 3}));

        engine.reapDesktopState(2);
        QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{1, 3}));
        // Repeat on the same, now-dead index: inert, and above all it must not
        // recreate a state for 2 on the way through.
        engine.reapDesktopState(2);
        QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{1, 3}));

        // KWin renumbers after the removal: 3 becomes 2. The renumber must see
        // the post-reap key set, so 3 moves down and nothing lands on a
        // resurrected 2.
        QHash<int, int> mapping;
        mapping.insert(3, 2);
        engine.renumberDesktopState(mapping);
        QCOMPARE(engine.desktopsWithActiveState(), (QSet<int>{1, 2}));
        QVERIFY(engine.isWindowTracked(QStringLiteral("app|d3")));
        QVERIFY(!engine.isWindowTracked(QStringLiteral("app|d2")));
    }

    // Index reuse: the number a reaped desktop held comes straight back when
    // the user makes another workspace. A state built on the reused index must
    // be a fresh one, holding only the new window.
    void reapDesktopState_reusedIndexStartsClean()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        PhosphorEngine::WindowRegistry registry;
        engine.setWindowRegistry(&registry);
        const QString screen = QStringLiteral("DP-1");
        engine.setAutotileScreens({screen});

        engine.setCurrentDesktopForScreen(screen, 2);
        engine.windowOpened(QStringLiteral("app|old"), screen);
        QVERIFY(engine.desktopsWithActiveState().contains(2));

        engine.reapDesktopState(2);
        QVERIFY(!engine.desktopsWithActiveState().contains(2));
        QVERIFY(!engine.isWindowTracked(QStringLiteral("app|old")));

        // Desktop number 2 is handed out again.
        engine.windowOpened(QStringLiteral("app|new"), screen);
        QVERIFY(engine.desktopsWithActiveState().contains(2));
        QVERIFY(engine.isWindowTracked(QStringLiteral("app|new")));
        QVERIFY(!engine.isWindowTracked(QStringLiteral("app|old")));
    }
};

QTEST_GUILESS_MAIN(TestAutotileEngineWorkspaces)
#include "test_autotile_engine_workspaces.moc"
