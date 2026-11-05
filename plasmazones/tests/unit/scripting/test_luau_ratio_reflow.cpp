// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reflow-on-resize for the ratio-based bundled algorithms (master-stack, wide,
// focus-sidebar, zen, deck, horizontal-deck). Dragging the master/boundary edge
// maps to a new split ratio: the onWindowResized hook returns it and
// LuauTileAlgorithm applies it to the state. These load the REAL bundled scripts.

#include <QtTest>

#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauWatchdog.h>

#include <memory>

using namespace PhosphorTiles;

class TestLuauRatioReflow : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_watchdog;

    static QString algoPath(const QString& file)
    {
        return QStringLiteral(P_SOURCE_DIR "/data/algorithms/") + file;
    }

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

    // ── master-stack: vertical master/stack seam ────────────────────────────────

    void masterStack_masterRightEdge_setsRatio()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("master-stack.luau")), m_watchdog);
        QVERIFY(algo.isValid());
        QVERIFY(algo.supportsResizeHook());
        QVERIFY(!algo.supportsMemory());

        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setMasterCount(1);
        state.setSplitRatio(0.5);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        QCOMPARE(z.size(), 2);
        const QRect master = z[0];
        QVERIFY(master.width() > 0);

        // Drag the master window's right edge 100px out → master/stack seam moves.
        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = master;
        ev.newRect = QRect(master.x(), master.y(), master.width() + 100, master.height());
        ev.right = true;
        algo.onWindowResized(&state, ev);

        const qreal expected = double(master.width() + 100) * 0.5 / master.width();
        QVERIFY(qAbs(state.splitRatio() - expected) < 0.005);
        QVERIFY(state.splitRatio() > 0.5); // master grew
    }

    void masterStack_stackLeftEdge_setsRatio()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("master-stack.luau")), m_watchdog);
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setMasterCount(1);
        state.setSplitRatio(0.5);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        const QRect stack = z[1];
        QVERIFY(stack.width() > 0);

        // Drag the stack window's left edge 80px left → stack grows, master shrinks.
        ResizeEvent ev;
        ev.index = 1;
        ev.oldRect = stack;
        ev.newRect = QRect(stack.x() - 80, stack.y(), stack.width() + 80, stack.height());
        ev.left = true;
        algo.onWindowResized(&state, ev);

        const qreal expected = 1.0 - double(stack.width() + 80) * 0.5 / stack.width();
        QVERIFY(qAbs(state.splitRatio() - expected) < 0.005);
        QVERIFY(state.splitRatio() < 0.5); // master shrank
    }

    // ── wide: horizontal master/stack seam (height axis) ────────────────────────

    void wide_masterBottomEdge_setsRatio()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("wide.luau")), m_watchdog);
        QVERIFY(algo.supportsResizeHook());
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setMasterCount(1);
        state.setSplitRatio(0.5);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        const QRect master = z[0];
        QVERIFY(master.height() > 0);

        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = master;
        ev.newRect = QRect(master.x(), master.y(), master.width(), master.height() + 120);
        ev.bottom = true;
        algo.onWindowResized(&state, ev);

        const qreal expected = double(master.height() + 120) * 0.5 / master.height();
        QVERIFY(qAbs(state.splitRatio() - expected) < 0.005);
        QVERIFY(state.splitRatio() > 0.5);
    }

    // ── zen: centred column, 2x compensation ────────────────────────────────────

    void zen_edge_appliesDoubleDelta()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("zen.luau")), m_watchdog);
        QVERIFY(algo.supportsResizeHook());
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setSplitRatio(0.6);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        const QRect col = z[0];
        QVERIFY(col.width() > 0);

        // Drag a window's right edge 60px out. The column is centre-anchored, so
        // the column width must change by 2*60 for the edge to land where released.
        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = col;
        ev.newRect = QRect(col.x(), col.y(), col.width() + 60, col.height());
        ev.right = true;
        algo.onWindowResized(&state, ev);

        const qreal expected = double(2 * (col.width() + 60) - col.width()) * 0.6 / col.width();
        QVERIFY(qAbs(state.splitRatio() - expected) < 0.005);
    }

    // ── deck: only the focused window (index 0) reflows; peeks no-op ─────────────

    void deck_focusedRightEdge_setsRatio()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("deck.luau")), m_watchdog);
        QVERIFY(algo.supportsResizeHook());
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setSplitRatio(0.75);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        const QRect focused = z[0];

        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = focused;
        ev.newRect = QRect(focused.x(), focused.y(), focused.width() - 100, focused.height());
        ev.right = true;
        algo.onWindowResized(&state, ev);

        const qreal expected = double(focused.width() - 100) * 0.75 / focused.width();
        QVERIFY(qAbs(state.splitRatio() - expected) < 0.005);
        QVERIFY(state.splitRatio() < 0.75); // focused shrank
    }

    void deck_peekWindow_isNoOp()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("deck.luau")), m_watchdog);
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setSplitRatio(0.75);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        const qreal before = state.splitRatio();

        // A peek window (index 1) cannot be resized independently → ratio unchanged.
        ResizeEvent ev;
        ev.index = 1;
        ev.oldRect = z[1];
        ev.newRect = QRect(z[1].x(), z[1].y(), z[1].width() + 100, z[1].height());
        ev.right = true;
        algo.onWindowResized(&state, ev);

        QVERIFY(qFuzzyCompare(1.0 + state.splitRatio(), 1.0 + before));
    }

    // A single window has no boundary → resize is a no-op.
    void singleWindow_isNoOp()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("master-stack.luau")), m_watchdog);
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.setMasterCount(1);
        state.setSplitRatio(0.5);

        const QVector<QRect> z = algo.calculateZones(params(&state, 1));
        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = z[0];
        ev.newRect = QRect(z[0].x(), z[0].y(), z[0].width() - 50, z[0].height());
        ev.right = true;
        algo.onWindowResized(&state, ev);

        QVERIFY(qFuzzyCompare(1.0 + state.splitRatio(), 1.0 + 0.5));
    }

    // ── focus-sidebar: vertical main/sidebar seam ───────────────────────────────

    void focusSidebar_mainRightEdge_setsRatio()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("focus-sidebar.luau")), m_watchdog);
        QVERIFY(algo.supportsResizeHook());
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setSplitRatio(0.7);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        const QRect main = z[0];
        QVERIFY(main.width() > 0);

        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = main;
        ev.newRect = QRect(main.x(), main.y(), main.width() + 100, main.height());
        ev.right = true;
        algo.onWindowResized(&state, ev);

        const qreal expected = double(main.width() + 100) * 0.7 / main.width();
        QVERIFY(qAbs(state.splitRatio() - expected) < 0.005);
        QVERIFY(state.splitRatio() > 0.7);
    }

    void focusSidebar_sidebarLeftEdge_setsRatio()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("focus-sidebar.luau")), m_watchdog);
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setSplitRatio(0.7);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        const QRect sidebar = z[1];
        QVERIFY(sidebar.width() > 0);

        // Drag the sidebar window's left edge 80px left → sidebar grows, main shrinks.
        ResizeEvent ev;
        ev.index = 1;
        ev.oldRect = sidebar;
        ev.newRect = QRect(sidebar.x() - 80, sidebar.y(), sidebar.width() + 80, sidebar.height());
        ev.left = true;
        algo.onWindowResized(&state, ev);

        const qreal expected = 1.0 - double(sidebar.width() + 80) * (1.0 - 0.7) / sidebar.width();
        QVERIFY(qAbs(state.splitRatio() - expected) < 0.005);
        QVERIFY(state.splitRatio() < 0.7);
    }

    // ── horizontal-deck: focused window's bottom edge (height axis) ──────────────

    void horizontalDeck_focusedBottomEdge_setsRatio()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("horizontal-deck.luau")), m_watchdog);
        QVERIFY(algo.supportsResizeHook());
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setSplitRatio(0.75);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        const QRect focused = z[0];
        QVERIFY(focused.height() > 0);

        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = focused;
        ev.newRect = QRect(focused.x(), focused.y(), focused.width(), focused.height() + 80);
        ev.bottom = true;
        algo.onWindowResized(&state, ev);

        const qreal expected = double(focused.height() + 80) * 0.75 / focused.height();
        QVERIFY(qAbs(state.splitRatio() - expected) < 0.005);
        QVERIFY(state.splitRatio() > 0.75);
    }

    // An orthogonal-axis drag (a horizontal-seam algorithm getting a vertical-edge
    // move) maps to no boundary → no-op.
    void masterStack_orthogonalEdge_isNoOp()
    {
        LuauTileAlgorithm algo(algoPath(QStringLiteral("master-stack.luau")), m_watchdog);
        TilingState state(QStringLiteral("s"));
        state.addWindow(QStringLiteral("w0"));
        state.addWindow(QStringLiteral("w1"));
        state.setMasterCount(1);
        state.setSplitRatio(0.5);

        const QVector<QRect> z = algo.calculateZones(params(&state, 2));
        // master-stack's seam is vertical; a top/bottom edge drag must not move the ratio.
        ResizeEvent ev;
        ev.index = 0;
        ev.oldRect = z[0];
        ev.newRect = QRect(z[0].x(), z[0].y(), z[0].width(), z[0].height() - 60);
        ev.bottom = true;
        algo.onWindowResized(&state, ev);

        QVERIFY(qFuzzyCompare(1.0 + state.splitRatio(), 1.0 + 0.5));
    }
};

QTEST_MAIN(TestLuauRatioReflow)
#include "test_luau_ratio_reflow.moc"
