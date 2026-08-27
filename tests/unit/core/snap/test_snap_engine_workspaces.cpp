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
};

QTEST_GUILESS_MAIN(TestSnapEngineWorkspaces)
#include "test_snap_engine_workspaces.moc"
