// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers/SnapEngineTestFixture.h"

/**
 * @brief Snap engine dynamic-workspaces arms: identity-based reap and
 *        renumber must reach the VALUE-side desktop ints snap keeps per
 *        window (assignments + last-used memo) inside every state — the
 *        key-level prune alone cannot, and the global holder is deliberately
 *        exempt from key rewrites. Mutation-style: after a pass, no stale
 *        pre-shift int may survive, and the 0 (sticky) sentinel never moves.
 */
class TestSnapEngineWorkspaces : public SnapEngineTestFixture
{
    Q_OBJECT

private Q_SLOTS:

    void reapDesktopState_dropsValueTagsForTheDeadDesktop()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        SnapState* state = engine.snapState();
        QVERIFY(state);

        state->recordResidence(QStringLiteral("w2"), QStringLiteral("DP-1"), 2);
        state->recordResidence(QStringLiteral("w3"), QStringLiteral("DP-1"), 3);
        state->recordResidence(QStringLiteral("sticky"), QStringLiteral("DP-1"), 0);

        engine.reapDesktopState(2);
        QVERIFY(!state->desktopAssignments().contains(QStringLiteral("w2")));
        QCOMPARE(state->desktopAssignments().value(QStringLiteral("w3")), 3);
        QCOMPARE(state->desktopAssignments().value(QStringLiteral("sticky")), 0);
    }

    void renumberDesktopState_shiftsValueTagsAndKeepsSentinel()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        SnapState* state = engine.snapState();
        QVERIFY(state);

        state->recordResidence(QStringLiteral("w3"), QStringLiteral("DP-1"), 3);
        state->recordResidence(QStringLiteral("w4"), QStringLiteral("DP-1"), 4);
        state->recordResidence(QStringLiteral("sticky"), QStringLiteral("DP-1"), 0);

        // Desktop 2 died: 3→2, 4→3.
        QHash<int, int> mapping;
        mapping.insert(3, 2);
        mapping.insert(4, 3);
        engine.renumberDesktopState(mapping);

        QCOMPARE(state->desktopAssignments().value(QStringLiteral("w3")), 2);
        QCOMPARE(state->desktopAssignments().value(QStringLiteral("w4")), 3);
        QCOMPARE(state->desktopAssignments().value(QStringLiteral("sticky")), 0);
        // Mutation guard: no window still carries a pre-shift 4.
        const auto assignments = state->desktopAssignments();
        for (auto it = assignments.constBegin(); it != assignments.constEnd(); ++it) {
            QVERIFY(it.value() != 4);
        }
    }

    void globalHolder_survivesDesktopLifecycle()
    {
        // The global holder's key (empty screenId, desktop 1) is a sentinel,
        // not a desktop context: renumbering must not rewrite it and reaping
        // must not delete it, through the ENGINE entry points (the store-level
        // skip predicate is covered by the PerScreenStates tests; this locks
        // the engine's use of it).
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        SnapState* globals = engine.snapState();
        QVERIFY(globals);
        globals->recordResidence(QStringLiteral("w"), QStringLiteral("DP-1"), 3);

        QHash<int, int> mapping;
        mapping.insert(1, 2); // would drag the sentinel key if the exemption dropped
        mapping.insert(3, 2);
        engine.renumberDesktopState(mapping);
        engine.reapDesktopState(1);
        engine.reapDesktopState(2);

        // Still the same holder, reachable through the engine, its value-side
        // tags lifecycled (w's desktop followed 3→2, then died with 2).
        QCOMPARE(engine.snapState(), globals);
        QVERIFY(!globals->desktopAssignments().contains(QStringLiteral("w")));
    }

    // The KEY-level arm of the reap, on a REAL per-(screen, desktop) store —
    // the arm dynamic workspaces made populated, and the one the global-holder
    // cases above cannot reach (the holder is excluded from the prune by its
    // empty screenId). Reaping the store's desktop must destroy the store AND
    // run the full release for its still-alive windows, not merely erase keys.
    void reapDesktopState_destroysScreenKeyedStateAndReleasesItsWindows()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);

        const QString screen = QStringLiteral("DP-1");
        engine.setCurrentDesktopForScreen(screen, 2);
        SnapState* state = engine.stateForWindowOnScreen(QStringLiteral("w2"), screen);
        QVERIFY(state);
        QVERIFY(state != engine.snapState()); // a real per-key store, not the holder
        state->setFloatingOnScreen(QStringLiteral("w2"), screen, 2);
        QVERIFY(engine.isWindowTracked(QStringLiteral("w2")));
        QVERIFY(engine.desktopsWithActiveState().contains(2));

        // A second store on a surviving desktop, to prove the reap is scoped.
        engine.setCurrentDesktopForScreen(screen, 3);
        SnapState* survivor = engine.stateForWindowOnScreen(QStringLiteral("w3"), screen);
        QVERIFY(survivor);
        survivor->setFloatingOnScreen(QStringLiteral("w3"), screen, 3);

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        engine.reapDesktopState(2);

        // Key level: the dead desktop's store is gone, the survivor's is not.
        const QSet<int> active = engine.desktopsWithActiveState();
        QVERIFY(!active.contains(2));
        QVERIFY(active.contains(3));

        // Release level: w2's placement was announced as released and its
        // reverse-map entry dropped. w3 was left entirely alone.
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.last().at(0).toString(), QStringLiteral("w2"));
        QCOMPARE(floatSpy.last().at(1).toBool(), false);
        QCOMPARE(floatSpy.last().at(2).toString(), screen);
        QVERIFY(!engine.isWindowTracked(QStringLiteral("w2")));
        QVERIFY(engine.isWindowTracked(QStringLiteral("w3")));
    }

    // The reap fires continuously in normal use (every workspace the user
    // empties), so drive it twice and then reuse the reaped index: a second
    // reap of an already-dead desktop must be inert, and a store created on a
    // reused index afterwards must survive its own reap cycle rather than
    // inheriting anything from the dead one.
    void reapDesktopState_repeatsAndSurvivesIndexReuse()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);

        const QString screen = QStringLiteral("DP-1");
        engine.setCurrentDesktopForScreen(screen, 2);
        SnapState* first = engine.stateForWindowOnScreen(QStringLiteral("w2"), screen);
        QVERIFY(first);
        first->setFloatingOnScreen(QStringLiteral("w2"), screen, 2);

        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);
        engine.reapDesktopState(2);
        QCOMPARE(floatSpy.count(), 1);

        // Second reap of the same, now-dead index: nothing left to release.
        engine.reapDesktopState(2);
        QCOMPARE(floatSpy.count(), 1);
        QVERIFY(!engine.desktopsWithActiveState().contains(2));

        // Index reuse: KWin hands desktop number 2 to a new workspace. A store
        // built there must be a fresh one and must live until it is reaped.
        SnapState* reused = engine.stateForWindowOnScreen(QStringLiteral("wNew"), screen);
        QVERIFY(reused);
        reused->setFloatingOnScreen(QStringLiteral("wNew"), screen, 2);
        QVERIFY(engine.desktopsWithActiveState().contains(2));
        QVERIFY(engine.isWindowTracked(QStringLiteral("wNew")));

        engine.reapDesktopState(2);
        QCOMPARE(floatSpy.count(), 2);
        QCOMPARE(floatSpy.last().at(0).toString(), QStringLiteral("wNew"));
        QVERIFY(!engine.desktopsWithActiveState().contains(2));
    }
};

QTEST_GUILESS_MAIN(TestSnapEngineWorkspaces)
#include "test_snap_engine_workspaces.moc"
