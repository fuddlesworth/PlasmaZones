// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrolling_adaptor_map.cpp
 * @brief The placement-map surfaces of ScrollingAdaptor.
 *
 * The Phosphor shell's placement map draws the WHOLE strip and lets the
 * user click a column or drag the view. This file pins the three verbs it
 * rides on, on the shared fixture (scrollingadaptortestfixture.h):
 *
 *  1. stripModelJson carries the same ownership gates as visibleStripJson,
 *     answers a valid empty model for an owned screen with no strip, and
 *     round-trips a small strip: every column with its strip position and
 *     extent, the cumulative positions consistent with the strip extent,
 *     the active column, every tile with its cross extent, and a tabbed
 *     column's display flag with its hidden tab sharing the shown tab's
 *     extent. Every number is cross-checked against the engine's own model.
 *  2. focusColumnAt refuses a foreign screen, a negative index and a closed
 *     context gate silently, focuses the named column (clamping an index
 *     past the end), and answers a press on the active column with the
 *     no-target feedback rather than silence.
 *  3. scrollViewByPx refuses zero and the same gates silently, and pans by
 *     exactly the pixel count it was given, detaching the view.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "dbus/scrollingadaptor/scrollingadaptor.h"
#include "scrollingadaptortestfixture.h"

using namespace PlasmaZones;
namespace Key = PhosphorProtocol::Service::StripModelKey;

class TestScrollingAdaptorMap : public QObject, protected ScrollingAdaptorTestFixture
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        setUpFixture();
    }

    void cleanup()
    {
        tearDownFixture();
    }

    void testStripModelJson_gatesAndEmptyStrip()
    {
        // Owned but never populated: a VALID model with no columns, carrying
        // the axis and viewport so a map can draw the empty strip. The
        // fixture's DP-1 work area is 1200 wide and 760 tall, so horizontal.
        const QJsonObject empty = parse(m_adaptor->stripModelJson(QStringLiteral("DP-1")));
        QVERIFY(!empty.isEmpty());
        QCOMPARE(empty.value(Key::Axis).toInt(), 0);
        QCOMPARE(empty.value(Key::ViewportPx).toInt(), 1200);
        QCOMPARE(empty.value(Key::ActiveColumn).toInt(), -1);
        QCOMPARE(empty.value(Key::StripExtentPx).toInt(), 0);
        QVERIFY(empty.value(Key::Columns).toArray().isEmpty());

        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);

        // The ownership gate and the wire-boundary check, both "{}" (the
        // documented answer, not an empty-but-valid model).
        QCOMPARE(m_adaptor->stripModelJson(QStringLiteral("HDMI-2")), QStringLiteral("{}"));
        QCOMPARE(m_adaptor->stripModelJson(QString()), QStringLiteral("{}"));

        // Positive control: the owned screen describes its column.
        const QString payload = m_adaptor->stripModelJson(QStringLiteral("DP-1"));
        QCOMPARE(parse(payload).value(Key::Columns).toArray().size(), 1);
        QVERIFY2(!payload.contains(QLatin1Char('\n')) && !payload.contains(QLatin1Char(' ')),
                 qPrintable(QStringLiteral("payload is not compact JSON: %1").arg(payload)));
    }

    void testStripModelJson_roundTripsAStrip()
    {
        // Three half-width columns overflow the 1200px viewport, so the
        // strip extent exceeds the viewport and the view offset is non-zero
        // once the last column takes focus.
        for (const char* id : {"app|a", "app|b", "app|c"}) {
            m_engine->windowOpened(QString::fromLatin1(id), QStringLiteral("DP-1"), 0, 0);
        }
        const PhosphorScrollEngine::ScrollStripModel model = m_engine->stripModelForScreen(QStringLiteral("DP-1"));
        QCOMPARE(model.columns.size(), 3);

        const QJsonObject obj = parse(m_adaptor->stripModelJson(QStringLiteral("DP-1")));
        QCOMPARE(obj.value(Key::Axis).toInt(), 0);
        QCOMPARE(obj.value(Key::ViewOffsetPx).toInt(), model.viewOffsetPx);
        QCOMPARE(obj.value(Key::ViewportPx).toInt(), 1200);
        QCOMPARE(obj.value(Key::StripExtentPx).toInt(), model.stripExtentPx);
        QCOMPARE(obj.value(Key::ActiveColumn).toInt(), model.activeColumn);
        // The last opened column holds focus, so the map's active column is
        // the last one and the view sits past the strip's start.
        QCOMPARE(obj.value(Key::ActiveColumn).toInt(), 2);
        QVERIFY(obj.value(Key::StripExtentPx).toInt() > 1200);
        QVERIFY(obj.value(Key::ViewOffsetPx).toInt() > 0);

        const QJsonArray columns = obj.value(Key::Columns).toArray();
        QCOMPARE(columns.size(), 3);
        int expectedPos = 0;
        const QStringList ids = {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")};
        for (int i = 0; i < columns.size(); ++i) {
            const QJsonObject col = columns.at(i).toObject();
            const PhosphorScrollEngine::ScrollStripModelColumn& expected = model.columns.at(i);
            QCOMPARE(col.value(Key::Index).toInt(), i);
            QCOMPARE(col.value(Key::StripPosPx).toInt(), expected.stripPosPx);
            QCOMPARE(col.value(Key::ExtentPx).toInt(), expected.extentPx);
            QCOMPARE(col.value(Key::Display).toInt(), 0);
            QCOMPARE(col.value(Key::ActiveTile).toInt(), 0);
            QCOMPARE(col.value(Key::Maximized).toBool(), false);
            // Cumulative positions: each column starts where the previous
            // one ended plus one gap, which is the strip-coordinate contract
            // the map lays columns out by. Pinned against the JSON itself
            // rather than only against the engine's copy of the same walk.
            QCOMPARE(col.value(Key::StripPosPx).toInt(), expectedPos);
            QVERIFY(col.value(Key::ExtentPx).toInt() > 0);
            expectedPos += col.value(Key::ExtentPx).toInt();
            if (i + 1 < columns.size()) {
                expectedPos += columns.at(i + 1).toObject().value(Key::StripPosPx).toInt()
                    - (col.value(Key::StripPosPx).toInt() + col.value(Key::ExtentPx).toInt());
            }
            const QJsonArray tiles = col.value(Key::Tiles).toArray();
            QCOMPARE(tiles.size(), 1);
            const QJsonObject tile = tiles.at(0).toObject();
            QCOMPARE(tile.value(Key::WindowId).toString(), ids.at(i));
            QCOMPARE(tile.value(Key::CrossPx).toInt(), expected.tiles.at(0).crossPx);
            QVERIFY(tile.value(Key::CrossPx).toInt() > 0);
            QCOMPARE(tile.value(Key::Minimized).toBool(), false);
        }
        // The strip ends where the last column ends.
        const QJsonObject last = columns.at(2).toObject();
        QCOMPARE(obj.value(Key::StripExtentPx).toInt(),
                 last.value(Key::StripPosPx).toInt() + last.value(Key::ExtentPx).toInt());
    }

    void testStripModelJson_tabbedColumnAndStack()
    {
        // Two windows in one column: consume b into a's column, then tab it.
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        m_engine->focusColumnFirst(QStringLiteral("DP-1"));
        m_engine->consumeWindowIntoColumn(QStringLiteral("DP-1"));

        QJsonObject obj = parse(m_adaptor->stripModelJson(QStringLiteral("DP-1")));
        QJsonArray columns = obj.value(Key::Columns).toArray();
        QCOMPARE(columns.size(), 1);
        QJsonObject col = columns.at(0).toObject();
        QCOMPARE(col.value(Key::Display).toInt(), 0);
        QJsonArray tiles = col.value(Key::Tiles).toArray();
        QCOMPARE(tiles.size(), 2);
        // A stack splits the cross extent: each tile is shorter than the
        // work area, and the two together with one inner gap fill it.
        const int stackedA = tiles.at(0).toObject().value(Key::CrossPx).toInt();
        const int stackedB = tiles.at(1).toObject().value(Key::CrossPx).toInt();
        QVERIFY(stackedA > 0 && stackedA < 760);
        QVERIFY(stackedB > 0 && stackedB < 760);

        m_engine->toggleColumnTabbed(QStringLiteral("DP-1"));
        obj = parse(m_adaptor->stripModelJson(QStringLiteral("DP-1")));
        col = obj.value(Key::Columns).toArray().at(0).toObject();
        QCOMPARE(col.value(Key::Display).toInt(), 1);
        tiles = col.value(Key::Tiles).toArray();
        QCOMPARE(tiles.size(), 2);
        // Every tab is committed at the column's one rect, so the hidden tab
        // carries the shown tab's extent rather than 0, and the two agree.
        const int tabbedA = tiles.at(0).toObject().value(Key::CrossPx).toInt();
        const int tabbedB = tiles.at(1).toObject().value(Key::CrossPx).toInt();
        QVERIFY(tabbedA > 0);
        QCOMPARE(tabbedA, tabbedB);
        QVERIFY(tabbedA > stackedA);
        // activeTile names the shown tab, which the model agrees on.
        const PhosphorScrollEngine::ScrollStripModel model = m_engine->stripModelForScreen(QStringLiteral("DP-1"));
        QCOMPARE(col.value(Key::ActiveTile).toInt(), model.columns.at(0).activeTile);
    }

    void testFocusColumnAt_gatesAndClamp()
    {
        for (const char* id : {"app|a", "app|b", "app|c"}) {
            m_engine->windowOpened(QString::fromLatin1(id), QStringLiteral("DP-1"), 0, 0);
        }
        QSignalSpy activateSpy(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
        QSignalSpy feedback(m_engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);

        m_adaptor->focusColumnAt(QStringLiteral("HDMI-2"), 0); // not ours
        m_adaptor->focusColumnAt(QString(), 0); // no screen at all
        m_adaptor->focusColumnAt(QStringLiteral("DP-1"), -1); // out of contract
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(feedback.count(), 0);

        // The per-context gate, per call and keyed on the screen, failing
        // closed with no gate installed (the same terms as focusColumn).
        m_adaptor->setContextGateProvider([](const QString& screenId) {
            return screenId == QStringLiteral("DP-1");
        });
        m_adaptor->focusColumnAt(QStringLiteral("DP-1"), 0);
        m_adaptor->setContextGateProvider({});
        m_adaptor->focusColumnAt(QStringLiteral("DP-1"), 0);
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(feedback.count(), 0);
        m_adaptor->setContextGateProvider([](const QString&) {
            return false;
        });

        // app|c holds focus (opened last). Index 0 lands on app|a.
        m_adaptor->focusColumnAt(QStringLiteral("DP-1"), 0);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.at(0).at(0).toString(), QStringLiteral("app|a"));
        QCOMPARE(feedback.count(), 1);
        QCOMPARE(feedback.last().at(0).toBool(), true);
        QCOMPARE(m_engine->managedFocusedWindow(QStringLiteral("DP-1")), QStringLiteral("app|a"));

        // Past the end clamps to the last column, the strip's own rule.
        m_adaptor->focusColumnAt(QStringLiteral("DP-1"), 99);
        QCOMPARE(activateSpy.count(), 2);
        QCOMPARE(activateSpy.at(1).at(0).toString(), QStringLiteral("app|c"));

        // The already-active column: no activation, and the no-target
        // feedback rather than silence, like the sibling focus verbs.
        m_adaptor->focusColumnAt(QStringLiteral("DP-1"), 2);
        QCOMPARE(activateSpy.count(), 2);
        QCOMPARE(feedback.count(), 3);
        QCOMPARE(feedback.last().at(0).toBool(), false);
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));
    }

    void testMoveColumnTo_gatesAndReorders()
    {
        // Four columns in open order; app|d holds focus.
        for (const char* id : {"app|a", "app|b", "app|c", "app|d"}) {
            m_engine->windowOpened(QString::fromLatin1(id), QStringLiteral("DP-1"), 0, 0);
        }
        QSignalSpy activateSpy(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
        QSignalSpy feedback(m_engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);

        // Silent refusals: foreign screen, no screen, either index negative,
        // and the closed context gate.
        m_adaptor->moveColumnTo(QStringLiteral("HDMI-2"), 0, 2);
        m_adaptor->moveColumnTo(QString(), 0, 2);
        m_adaptor->moveColumnTo(QStringLiteral("DP-1"), -1, 2);
        m_adaptor->moveColumnTo(QStringLiteral("DP-1"), 0, -1);
        m_adaptor->setContextGateProvider([](const QString&) {
            return true;
        });
        m_adaptor->moveColumnTo(QStringLiteral("DP-1"), 0, 2);
        m_adaptor->setContextGateProvider([](const QString&) {
            return false;
        });
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(feedback.count(), 0);
        QCOMPARE(columnOrder(),
                 (QStringList{QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c"),
                              QStringLiteral("app|d")}));

        // Move the first column to index 2: b, c, a, d. The moved column
        // becomes active and its window is activated, the map's drop.
        m_adaptor->moveColumnTo(QStringLiteral("DP-1"), 0, 2);
        QCOMPARE(columnOrder(),
                 (QStringList{QStringLiteral("app|b"), QStringLiteral("app|c"), QStringLiteral("app|a"),
                              QStringLiteral("app|d")}));
        QCOMPARE(m_engine->stripModelForScreen(QStringLiteral("DP-1")).activeColumn, 2);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.at(0).at(0).toString(), QStringLiteral("app|a"));
        QCOMPARE(feedback.count(), 1);
        QCOMPARE(feedback.last().at(0).toBool(), true);
        QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("move"));

        // Backwards, from the end: d moves to the front.
        m_adaptor->moveColumnTo(QStringLiteral("DP-1"), 3, 0);
        QCOMPARE(columnOrder(),
                 (QStringList{QStringLiteral("app|d"), QStringLiteral("app|b"), QStringLiteral("app|c"),
                              QStringLiteral("app|a")}));
        QCOMPARE(m_engine->stripModelForScreen(QStringLiteral("DP-1")).activeColumn, 0);
        QCOMPARE(activateSpy.count(), 2);
        QCOMPARE(activateSpy.at(1).at(0).toString(), QStringLiteral("app|d"));

        // No such move: from == to, or an index past the end. Nothing moves,
        // and unlike a focus index it does not clamp; the no-target feedback
        // reports the refusal rather than silence.
        m_adaptor->moveColumnTo(QStringLiteral("DP-1"), 1, 1);
        m_adaptor->moveColumnTo(QStringLiteral("DP-1"), 99, 0);
        m_adaptor->moveColumnTo(QStringLiteral("DP-1"), 0, 99);
        QCOMPARE(columnOrder(),
                 (QStringList{QStringLiteral("app|d"), QStringLiteral("app|b"), QStringLiteral("app|c"),
                              QStringLiteral("app|a")}));
        QCOMPARE(activateSpy.count(), 2);
        QCOMPARE(feedback.count(), 5);
        for (int i = 2; i < 5; ++i) {
            QCOMPARE(feedback.at(i).at(0).toBool(), false);
            QCOMPARE(feedback.at(i).at(2).toString(), QStringLiteral("no_target"));
        }
    }

    void testScrollViewByPx_gatesAndPansExactly()
    {
        // Same overflowing strip as the scrollView test: the view sits at
        // the END, so a backward pan has room and grows the anchor.
        for (const char* id : {"app|a", "app|b", "app|c"}) {
            m_engine->windowOpened(QString::fromLatin1(id), QStringLiteral("DP-1"), 0, 0);
            m_engine->windowFocused(QString::fromLatin1(id), QStringLiteral("DP-1"));
            m_engine->setColumnWidth(PhosphorScrollEngine::ColumnWidth::makeProportion(0.55), QStringLiteral("DP-1"));
        }
        auto* state = static_cast<PhosphorScrollEngine::ScrollState*>(m_engine->stateForScreen(QStringLiteral("DP-1")));
        QVERIFY(state);
        const int anchorBefore = state->strip().viewAnchor();
        QSignalSpy feedback(m_engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        QSignalSpy activateSpy(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);

        // Zero is refused at the boundary: no feedback, unlike the engine's
        // own no_movement answer to a zero-pixel percent.
        m_adaptor->scrollViewByPx(QStringLiteral("DP-1"), 0);
        m_adaptor->scrollViewByPx(QStringLiteral("HDMI-2"), -100); // not ours
        m_adaptor->scrollViewByPx(QString(), -100); // no screen at all
        QCOMPARE(state->strip().viewAnchor(), anchorBefore);
        QCOMPARE(feedback.count(), 0);

        m_adaptor->setContextGateProvider([](const QString& screenId) {
            return screenId == QStringLiteral("DP-1");
        });
        m_adaptor->scrollViewByPx(QStringLiteral("DP-1"), -100);
        m_adaptor->setContextGateProvider({});
        m_adaptor->scrollViewByPx(QStringLiteral("DP-1"), -100);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore);
        QCOMPARE(feedback.count(), 0);
        m_adaptor->setContextGateProvider([](const QString&) {
            return false;
        });

        // A forward pan from the end clamps: anchor holds, no_target answers.
        m_adaptor->scrollViewByPx(QStringLiteral("DP-1"), 100);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore);
        QCOMPARE(feedback.count(), 1);
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));

        // The positive control: EXACTLY the pixels asked for, no step
        // rounding, and the view detaches from the centering policy while
        // focus stays where it was.
        m_adaptor->scrollViewByPx(QStringLiteral("DP-1"), -137);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore + 137);
        QVERIFY(state->strip().viewDetached());
        QCOMPARE(feedback.count(), 2);
        QCOMPARE(feedback.last().at(0).toBool(), true);
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(m_engine->managedFocusedWindow(QStringLiteral("DP-1")), QStringLiteral("app|c"));
        // A second call is a second pan, and the model reports the moved view.
        m_adaptor->scrollViewByPx(QStringLiteral("DP-1"), -63);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore + 200);
        const PhosphorScrollEngine::ScrollStripModel model = m_engine->stripModelForScreen(QStringLiteral("DP-1"));
        QCOMPARE(parse(m_adaptor->stripModelJson(QStringLiteral("DP-1"))).value(Key::ViewOffsetPx).toInt(),
                 model.viewOffsetPx);
    }

private:
    static QJsonObject parse(const QString& json)
    {
        return QJsonDocument::fromJson(json.toUtf8()).object();
    }

    /// DP-1's columns in strip order, each named by its first tile.
    QStringList columnOrder() const
    {
        QStringList order;
        const PhosphorScrollEngine::ScrollStripModel model = m_engine->stripModelForScreen(QStringLiteral("DP-1"));
        for (const PhosphorScrollEngine::ScrollStripModelColumn& column : model.columns) {
            order.append(column.tiles.isEmpty() ? QString() : column.tiles.first().windowId);
        }
        return order;
    }
};

QTEST_GUILESS_MAIN(TestScrollingAdaptorMap)
#include "test_scrolling_adaptor_map.moc"
