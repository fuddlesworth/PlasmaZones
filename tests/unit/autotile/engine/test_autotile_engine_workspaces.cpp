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
};

QTEST_GUILESS_MAIN(TestAutotileEngineWorkspaces)
#include "test_autotile_engine_workspaces.moc"
