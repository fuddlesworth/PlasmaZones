// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// P4 exit criterion: the bundled aligned-grid.luau is the reference consumer of
// the interactive-resize scripting hook (P3). These tests load the REAL bundled
// algorithm and prove (a) the scripting resize path works end-to-end and (b) the
// thing a SplitTree can't do — resizing one cell moves the shared column/row
// boundary across every row/column.

#include <QtTest>

#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauWatchdog.h>

#include <memory>

using namespace PhosphorTiles;

class TestLuauAlignedGrid : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_watchdog;

    static QString gridPath()
    {
        return QStringLiteral(P_SOURCE_DIR "/data/algorithms/aligned-grid.luau");
    }

    // A 1000x1000 screen, 10px inner gap, no outer gaps → content area 990x990.
    static TilingParams params(TilingState* state, int count)
    {
        TilingParams p;
        p.windowCount = count;
        p.screenGeometry = QRect(0, 0, 1000, 1000);
        p.state = state;
        p.innerGap = 10;
        return p;
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_watchdog = std::make_shared<PhosphorScripting::LuauWatchdog>();
    }

    void loadsAndDeclaresResizeHook()
    {
        LuauTileAlgorithm algo(gridPath(), m_watchdog);
        QVERIFY(algo.isValid());
        QVERIFY(algo.supportsResizeHook());
        QVERIFY(!algo.supportsMemory()); // it's script-state, not a SplitTree
    }

    // Default (no stored state) → an equal 2x2 grid with aligned columns.
    void uniformGridWhenNoState()
    {
        LuauTileAlgorithm algo(gridPath(), m_watchdog);
        TilingState state(QStringLiteral("s"));
        for (int i = 0; i < 4; ++i) {
            state.addWindow(QStringLiteral("w%1").arg(i));
        }
        const QVector<QRect> z = algo.calculateZones(params(&state, 4));
        QCOMPARE(z.size(), 4);
        // Column 0 (z[0] top, z[2] bottom) and column 1 (z[1], z[3]) are aligned.
        QCOMPARE(z[0].width(), z[2].width());
        QCOMPARE(z[1].width(), z[3].width());
        // Roughly equal halves.
        QVERIFY(qAbs(z[0].width() - z[1].width()) <= 2);
    }

    // The proof obligation: resize the top-left cell's right edge, and the shared
    // column boundary must move for BOTH rows (not just the dragged window's row).
    void resizeMovesColumnAcrossAllRows()
    {
        LuauTileAlgorithm algo(gridPath(), m_watchdog);
        TilingState state(QStringLiteral("s"));
        for (int i = 0; i < 4; ++i) {
            state.addWindow(QStringLiteral("w%1").arg(i));
        }

        const QVector<QRect> before = algo.calculateZones(params(&state, 4));
        QCOMPARE(before.size(), 4);
        const int col0Before = before[0].width();
        QVERIFY(state.scriptState().isEmpty()); // nothing stored yet

        // Drag w0's right edge 200px to the right.
        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = before[0];
        ev.newRect = QRect(before[0].x(), before[0].y(), before[0].width() + 200, before[0].height());
        ev.right = true;
        algo.onWindowResized(&state, ev);

        // The hook persisted the new column fractions.
        QVERIFY(!state.scriptState().isEmpty());

        const QVector<QRect> after = algo.calculateZones(params(&state, 4));
        QCOMPARE(after.size(), 4);
        // Column 0 grew...
        QVERIFY(after[0].width() > col0Before + 150);
        // ...by the same amount in BOTH rows (alignment preserved).
        QCOMPARE(after[0].width(), after[2].width());
        QCOMPARE(after[1].width(), after[3].width());
        // ...and column 1 shrank to compensate.
        QVERIFY(after[1].width() < before[1].width());
    }

    // A window add reshapes the grid (2x2 → 3x2); stored 2-column fractions no
    // longer match, so the layout resets to uniform rather than mis-indexing.
    void reshapeResetsToUniform()
    {
        LuauTileAlgorithm algo(gridPath(), m_watchdog);
        TilingState state(QStringLiteral("s"));
        for (int i = 0; i < 4; ++i) {
            state.addWindow(QStringLiteral("w%1").arg(i));
        }
        // Bias column 0 wide, then add a 5th window.
        const QVector<QRect> g = algo.calculateZones(params(&state, 4));
        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = g[0];
        ev.newRect = QRect(g[0].x(), g[0].y(), g[0].width() + 200, g[0].height());
        ev.right = true;
        algo.onWindowResized(&state, ev);
        state.addWindow(QStringLiteral("w4"));

        const QVector<QRect> z = algo.calculateZones(params(&state, 5)); // cols=3, rows=2
        QCOMPARE(z.size(), 5);
        // Top row's three columns are ~equal again (uniform reset), proving the
        // stale 2-column fractions were discarded.
        QVERIFY(qAbs(z[0].width() - z[1].width()) <= 2);
        QVERIFY(qAbs(z[1].width() - z[2].width()) <= 2);
    }
};

QTEST_MAIN(TestLuauAlignedGrid)
#include "test_luau_aligned_grid.moc"
