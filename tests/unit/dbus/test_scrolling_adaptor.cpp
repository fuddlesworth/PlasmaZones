// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrolling_adaptor.cpp
 * @brief Behaviour of ScrollingAdaptor, the org.plasmazones.Scrolling surface.
 *
 * The contract-sync test covers the XML's SHAPE only — that the declared
 * methods and signals exist with the right signatures. What it cannot check
 * is whether they do what their DocStrings promise, which is what this file
 * pins:
 *
 *  1. focusColumn and visibleStripJson both refuse a screen the engine does
 *     not own, so a wheel event or a preview query aimed at a non-scrolling
 *     monitor is never redirected onto the active scrolling one.
 *  2. visibleStripJson emits the rects the XML documents, INCLUDING
 *     zoneNumber, and normalizes them to 0..1 per axis.
 *  3. scrollingScreensChanged is gated on an actual change: the engine
 *     re-announces an identical set on every desktop switch, and a wire
 *     consumer comparing successive payloads must not see a phantom one.
 *  4. clearEngine leaves every slot answering safely.
 */

#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include "dbus/scrollingadaptor/scrollingadaptor.h"

using namespace PlasmaZones;
using PhosphorScrollEngine::ScrollEngine;

class TestScrollingAdaptor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_engine = new ScrollEngine(nullptr, nullptr, nullptr);
        // Headless geometry seam: without a work area the strip resolves no
        // rects at all, and visibleStripJson would return "[]" for every
        // screen — including the one it is supposed to describe.
        const auto geometry = [](const QString&) {
            return QRect(0, 0, 1200, 800);
        };
        m_engine->setScreenGeometryProviders(geometry, geometry);
        m_parent = new QObject(nullptr);
        m_adaptor = new ScrollingAdaptor(m_engine, m_parent);
        m_engine->setActiveScreens({QStringLiteral("DP-1")});
    }

    void cleanup()
    {
        m_adaptor->clearEngine();
        delete m_parent;
        m_parent = nullptr;
        m_adaptor = nullptr;
        delete m_engine;
        m_engine = nullptr;
    }

    // The XML promises an empty array for a screen with no strip or one that
    // is not scrolling. A screen the engine does not own must take the second
    // branch rather than describing the active screen's strip.
    void testVisibleStripJson_refusesForeignScreen()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);

        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("HDMI-2")), QStringLiteral("[]"));
        QCOMPARE(m_adaptor->visibleStripJson(QString()), QStringLiteral("[]"));

        // Positive control: the screen the engine DOES own describes a strip,
        // so the two refusals above are not just "this fixture has no rects".
        QVERIFY(m_adaptor->visibleStripJson(QStringLiteral("DP-1")) != QStringLiteral("[]"));
    }

    // After the screen leaves scrolling mode the preview goes empty. Two
    // things independently produce that — the engine releases the screen's
    // state, and the adaptor's isActiveOnScreen gate — so this pins the
    // OUTCOME the settings app depends on, not either mechanism. Removing
    // just the gate would not fail this, and that is accurate: the gate is
    // documented as belt and braces at its own call site.
    void testVisibleStripJson_emptyAfterModeExit()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        QVERIFY(m_adaptor->visibleStripJson(QStringLiteral("DP-1")) != QStringLiteral("[]"));

        m_engine->setActiveScreens({});
        QVERIFY(!m_engine->isActiveOnScreen(QStringLiteral("DP-1")));
        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
    }

    // The documented payload: normalized rects, each carrying the 1-based
    // visible column slot. zoneNumber is the field a consumer needs to map a
    // rect back to a scroll zone, and it is the one the DocString used to
    // omit.
    void testVisibleStripJson_shapeAndZoneNumbers()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);

        // Cross-check against the engine, so the payload is compared with an
        // INDEPENDENT source rather than with itself. The adaptor falls back
        // to `i + 1` when columnNumbers is shorter than rects, so asserting
        // only "the ordinals ascend" would still pass if the engine regressed
        // to supplying no column numbers at all.
        QVector<int> engineColumns;
        const QVector<QRectF> engineRects = m_engine->visibleTileRectsRelative(QStringLiteral("DP-1"), &engineColumns);
        QCOMPARE(engineRects.size(), 2);
        QCOMPARE(engineColumns.size(), engineRects.size());

        const QJsonDocument doc = QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8());
        QVERIFY(doc.isArray());
        const QJsonArray arr = doc.array();
        QCOMPARE(arr.size(), engineRects.size());

        // Failures accumulate rather than aborting on the first bad rect, so
        // one failure does not hide the state of the others.
        QStringList failures;
        for (int i = 0; i < arr.size(); ++i) {
            if (!arr.at(i).isObject()) {
                failures.append(QStringLiteral("rect %1: not an object").arg(i));
                continue;
            }
            const QJsonObject obj = arr.at(i).toObject();
            for (const QLatin1String key :
                 {QLatin1String("x"), QLatin1String("y"), QLatin1String("width"), QLatin1String("height")}) {
                if (!obj.contains(key)) {
                    failures.append(QStringLiteral("rect %1: missing %2").arg(i).arg(QString(key)));
                    continue;
                }
                const double v = obj.value(key).toDouble(-1.0);
                if (v < 0.0 || v > 1.0) {
                    failures.append(QStringLiteral("rect %1: %2 = %3 outside 0..1").arg(i).arg(QString(key)).arg(v));
                }
            }
            const int zoneNumber = obj.value(QLatin1String("zoneNumber")).toInt(-1);
            if (zoneNumber != engineColumns.at(i)) {
                failures.append(QStringLiteral("rect %1: zoneNumber %2 != engine %3")
                                    .arg(i)
                                    .arg(zoneNumber)
                                    .arg(engineColumns.at(i)));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));

        // The two-adjacent-columns fixture above yields ordinals {1, 2},
        // which is exactly `i + 1` — so it cannot tell the real column
        // numbers from the adaptor's index fallback. Stack both windows into
        // ONE column: two rects now share column ordinal 1, and any
        // regression to `i + 1` reports {1, 2} and fails here.
        // Focus the LEFT column first: consume pulls the neighbour to its
        // right into the active column, so with the last-opened window active
        // there is nothing to its right and the call is a no-op.
        m_engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("DP-1"));
        m_engine->consumeWindowIntoColumn(QStringLiteral("DP-1"));

        QVector<int> stackedColumns;
        const QVector<QRectF> stackedRects =
            m_engine->visibleTileRectsRelative(QStringLiteral("DP-1"), &stackedColumns);
        QCOMPARE(stackedRects.size(), 2);
        QCOMPARE(stackedColumns, (QVector<int>{1, 1}));

        const QJsonDocument stackedDoc =
            QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8());
        const QJsonArray stackedArr = stackedDoc.array();
        QCOMPARE(stackedArr.size(), 2);
        QCOMPARE(stackedArr.at(0).toObject().value(QLatin1String("zoneNumber")).toInt(-1), 1);
        QVERIFY2(stackedArr.at(1).toObject().value(QLatin1String("zoneNumber")).toInt(-1) == 1,
                 "both tiles of one column must report that column's ordinal, not their rect index");
    }

    // focusColumn's own doc: gated on the engine owning the screen, because
    // the engine's screen fallback would otherwise redirect a wheel event
    // from a non-scrolling monitor onto the active scrolling one.
    void testFocusColumn_ignoresForeignScreenAndBadDelta()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);

        QSignalSpy activateSpy(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);

        m_adaptor->focusColumn(QStringLiteral("HDMI-2"), -1); // not ours
        m_adaptor->focusColumn(QString(), -1); // no screen at all
        m_adaptor->focusColumn(QStringLiteral("DP-1"), 0); // not a direction
        m_adaptor->focusColumn(QStringLiteral("DP-1"), 2); // not a direction either
        QCOMPARE(activateSpy.count(), 0);

        // Positive control: the same call with a real screen and a real
        // direction DOES move focus, so the refusals above discriminate.
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 1);
    }

    // The engine re-emits an unchanged screen set on every desktop switch for
    // the tiling channel's benefit. This adaptor must not relay those.
    void testScreensChanged_suppressesUnchangedSets()
    {
        // The adaptor's `screenIds == m_lastBroadcastScreens` gate only earns
        // its keep against a payload the ENGINE actually re-sends. The engine
        // re-emits an identical set exactly once per armed desktop-context
        // switch (setCurrentDesktop / setCurrentDesktopForScreen /
        // setCurrentActivity set the flag, and every setActiveScreens entry
        // consumes it), so a plain repeat push emits nothing at all and the
        // gate is never reached. Arming it is what makes this test real:
        // without the arming calls below, deleting the gate outright leaves
        // the suite green.
        QSignalSpy engineSpy(m_engine, &ScrollEngine::scrollingScreensChanged);
        QSignalSpy spy(m_adaptor, &ScrollingAdaptor::scrollingScreensChanged);

        m_engine->setActiveScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toStringList(), (QStringList{QStringLiteral("DP-1"), QStringLiteral("DP-2")}));
        QCOMPARE(engineSpy.count(), 1);

        // Two desktop switches, each followed by a push of the SAME set. This
        // is the real-world shape: with per-context modes every switch
        // re-derives the scrolling set and pushes it, usually unchanged.
        //
        // The FIRST setCurrentDesktop only primes the tracker — armSwitch is
        // gated on the desktop context having been set before, so a fresh
        // engine's first call records the context without claiming a switch.
        // The second one arms, and its push is the identical-set re-emit the
        // adaptor has to absorb. The second set is also spelled in the other
        // order, which carries no information (the parameter is a QSet) but
        // matches how the daemon builds it.
        m_engine->setCurrentDesktop(2);
        m_engine->setActiveScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        m_engine->setCurrentDesktop(3);
        m_engine->setActiveScreens({QStringLiteral("DP-2"), QStringLiteral("DP-1")});

        // Positive control: the engine really did re-emit the unchanged set
        // once, so the adaptor's count staying at 1 below is the GATE working
        // rather than the engine staying silent. Without this pair the test
        // passes with the gate deleted outright.
        QCOMPARE(engineSpy.count(), 2);
        QCOMPARE(spy.count(), 1);

        // A genuine change does get through.
        m_engine->setActiveScreens({QStringLiteral("DP-1")});
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toStringList(), (QStringList{QStringLiteral("DP-1")}));
    }

    // Shutdown contract shared with the sibling adaptors: a late D-Bus call
    // after clearEngine answers safely instead of dereferencing the engine.
    void testClearedEngine_slotsAnswerSafely()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_adaptor->clearEngine();

        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
        QVERIFY(m_adaptor->scrollingScreens().isEmpty());
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1); // must not crash
    }

private:
    ScrollEngine* m_engine = nullptr;
    QObject* m_parent = nullptr;
    ScrollingAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestScrollingAdaptor)
#include "test_scrolling_adaptor.moc"
