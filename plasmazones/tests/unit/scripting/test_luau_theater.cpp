// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Geometry and resize behavior for the Theater widescreen layout: the focused
// window sits in a centered spotlight at a width you set per monitor, and the
// rest line up on rails in the margins on both sides. Loads the REAL bundled
// script from data/algorithms/.

#include <QtTest>

#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauWatchdog.h>

#include <memory>

using namespace PhosphorTiles;

class TestLuauTheater : public QObject
{
    Q_OBJECT

private:
    static constexpr int W = 1920;
    static constexpr int H = 1080;

    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_watchdog;

    static QString algoPath()
    {
        return QStringLiteral(P_SOURCE_DIR "/data/algorithms/theater.luau");
    }

    static TilingParams params(TilingState* state, int count, int focusedIndex = -1)
    {
        TilingParams p;
        p.windowCount = count;
        p.screenGeometry = QRect(0, 0, W, H);
        p.state = state;
        p.innerGap = 0;
        p.outerGaps = PhosphorLayout::EdgeGaps::uniform(0);
        p.focusedIndex = focusedIndex;
        return p;
    }

    static TilingState* makeState(int count)
    {
        auto* state = new TilingState(QStringLiteral("s"));
        for (int i = 0; i < count; ++i) {
            state->addWindow(QStringLiteral("w%1").arg(i));
        }
        return state;
    }

    static bool noOverlaps(const QVector<QRect>& zones)
    {
        for (int i = 0; i < zones.size(); ++i) {
            for (int j = i + 1; j < zones.size(); ++j) {
                if (zones[i].intersects(zones[j])) {
                    return false;
                }
            }
        }
        return true;
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_watchdog = std::make_shared<PhosphorScripting::LuauWatchdog>();
    }

    void loadsAndOwnsSingleWindow()
    {
        LuauTileAlgorithm algo(algoPath(), m_watchdog);
        QVERIFY(algo.isValid());
        QCOMPARE(algo.id(), QStringLiteral("theater"));
        QVERIFY(algo.supportsSingleWindow());
        QVERIFY(algo.supportsResizeHook());
        QVERIFY(algo.retilesOnFocusChange());
    }

    void singleWindow_centered()
    {
        LuauTileAlgorithm algo(algoPath(), m_watchdog);
        std::unique_ptr<TilingState> state(makeState(1));

        const QVector<QRect> z = algo.calculateZones(params(state.get(), 1));
        QCOMPARE(z.size(), 1);
        QCOMPARE(z[0].width(), 1152);
        QCOMPARE(z[0].height(), 918);
        QCOMPARE(z[0].center(), QRect(0, 0, W, H).center());
    }

    void spotlightFollowsFocus()
    {
        LuauTileAlgorithm algo(algoPath(), m_watchdog);
        std::unique_ptr<TilingState> state(makeState(3));

        // The spotlight zone is the centered one; it must land at the focused
        // index so the host (positional fill) puts the focused window there.
        for (int f = 0; f < 3; ++f) {
            const QVector<QRect> z = algo.calculateZones(params(state.get(), 3, f));
            QCOMPARE(z.size(), 3);
            QVERIFY(noOverlaps(z));

            const QRect spotlight = z[f];
            QCOMPARE(spotlight.width(), 1152);
            QCOMPARE(spotlight.height(), 918);
            QCOMPARE(spotlight.center().x(), QRect(0, 0, W, H).center().x());

            // The other two split across both margins: one rail window per side,
            // each a narrower full-height strip outside the spotlight.
            int leftRail = 0;
            int rightRail = 0;
            for (int i = 0; i < 3; ++i) {
                if (i == f) {
                    continue;
                }
                QCOMPARE(z[i].width(), 384);
                const bool inLeftMargin = z[i].x() + z[i].width() <= spotlight.x();
                const bool inRightMargin = z[i].x() >= spotlight.x() + spotlight.width();
                QVERIFY(inLeftMargin || inRightMargin);
                if (inLeftMargin) {
                    ++leftRail;
                } else {
                    ++rightRail;
                }
            }
            QCOMPARE(leftRail, 1);
            QCOMPARE(rightRail, 1);
        }
    }

    void unknownFocus_defaultsToFirst()
    {
        LuauTileAlgorithm algo(algoPath(), m_watchdog);
        std::unique_ptr<TilingState> state(makeState(2));

        const QVector<QRect> z = algo.calculateZones(params(state.get(), 2, -1));
        QCOMPARE(z.size(), 2);
        QCOMPARE(z[0].width(), 1152); // spotlight defaulted to index 0
        QVERIFY(z[1].x() > z[0].x());
        QCOMPARE(z[1].width(), 384);
    }

    void resizeSpotlight_remembersWidth()
    {
        LuauTileAlgorithm algo(algoPath(), m_watchdog);
        std::unique_ptr<TilingState> state(makeState(2));

        // Focus the second window; its zone is the spotlight at index 1. The
        // resize hook reads the focused index from the state, so set it there too.
        state->setFocusedWindow(QStringLiteral("w1"));
        const QVector<QRect> z0 = algo.calculateZones(params(state.get(), 2, 1));
        const QRect spotlight = z0[1];
        QCOMPARE(spotlight.width(), 1152);

        // A real interactive resize moves at most one edge per axis (left XOR
        // right); the host never emits both together. Drag the right edge only.
        ResizeEvent ev;
        ev.index = 1; // the focused/spotlight window
        ev.oldRect = spotlight;
        ev.newRect = QRect(spotlight.x(), spotlight.y(), spotlight.width() + 200, spotlight.height());
        ev.right = true;
        algo.onWindowResized(state.get(), ev);

        const QVector<QRect> z1 = algo.calculateZones(params(state.get(), 2, 1));
        QCOMPARE(z1[1].width(), spotlight.width() + 200);
        QCOMPARE(z1[1].center().x(), QRect(0, 0, W, H).center().x());
    }

    void resizeRailWindow_noops()
    {
        LuauTileAlgorithm algo(algoPath(), m_watchdog);
        std::unique_ptr<TilingState> state(makeState(3));

        const QVector<QRect> z0 = algo.calculateZones(params(state.get(), 3, 0));
        const QRect spotlightBefore = z0[0];

        // index 2 is a rail window (focus is 0); resizing it must change nothing.
        ResizeEvent ev;
        ev.index = 2;
        ev.oldRect = z0[2];
        ev.newRect = QRect(z0[2].x() - 100, z0[2].y(), z0[2].width() + 100, z0[2].height());
        ev.left = true;
        algo.onWindowResized(state.get(), ev);

        const QVector<QRect> z1 = algo.calculateZones(params(state.get(), 3, 0));
        QCOMPARE(z1[0], spotlightBefore);
    }
};

QTEST_MAIN(TestLuauTheater)
#include "test_luau_theater.moc"
