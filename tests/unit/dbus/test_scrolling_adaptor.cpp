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
 *  1. focusColumn refuses a screen the engine does not own, so a wheel event
 *     aimed at a non-scrolling monitor is never redirected onto the active
 *     scrolling one; and it maps its delta to the documented direction.
 *  2. visibleStripJson refuses the same way, and its gate is load-bearing:
 *     a strip belonging to a sibling desktop context OUTLIVES the screen
 *     leaving the scrolling set, so only the gate keeps the preview empty.
 *  3. visibleStripJson emits the rects the XML documents, INCLUDING
 *     zoneNumber, normalized to 0..1 per axis against the full screen
 *     geometry, cross-checked against the engine's own rects.
 *  4. The screenId argument is load-bearing: two owned screens with
 *     different strips produce different payloads.
 *  5. Off-screen columns are excluded at the wire boundary — the payload
 *     describes what is visible, not what is managed.
 *  6. scrollingScreens answers sorted, across several screens.
 *  7. scrollingScreensChanged is gated on an actual change: the engine
 *     re-announces an identical set on every desktop switch, and a wire
 *     consumer comparing successive payloads must not see a phantom one.
 *  8. clearEngine leaves every slot answering safely AND stops relaying.
 */

#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <PhosphorEngine/PerScreenKeys.h>
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
        // The two stubbed seams: no IWindowTrackingService (so window ids
        // stay raw, uncanonicalized) and no ScreenManager (so geometry
        // comes from the injected providers below instead of real outputs).
        m_engine = new ScrollEngine(nullptr, nullptr);
        // Headless geometry seam: without a work area the strip resolves no
        // rects at all, and visibleStripJson would return "[]" for every
        // screen — including the one it is supposed to describe.
        //
        // Neither provider is at the origin: a (0,0) work area would let a
        // dropped origin-subtraction term pass unnoticed (x/1920 lands well
        // outside 0..1). The two rects also differ so a reader can see which
        // basis the payload normalizes against — the FULL screen geometry,
        // with tiles clipped to the available one — though no assertion here
        // distinguishes them (a swap normalizes against 760 instead of 800
        // and stays inside 0..1).
        const auto available = [](const QString&) {
            return QRect(1920, 40, 1200, 760);
        };
        const auto screen = [](const QString&) {
            return QRect(1920, 0, 1200, 800);
        };
        m_engine->setScreenGeometryProviders(available, screen);
        // Well-behaved-compositor echo, same as ScrollTestUtils'
        // makeProviderEngine: every activation request is answered with a
        // windowFocused report so the engine's pending-self-activation queue
        // drains — without it the next simulated USER focus of that window is
        // consumed as the missing echo.
        connect(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, m_engine,
                [this](const QString& windowId) {
                    m_engine->windowFocused(windowId, m_engine->screenForTrackedWindow(windowId));
                });
        m_parent = new QObject(nullptr);
        m_adaptor = new ScrollingAdaptor(m_engine, m_parent);
        m_engine->setActiveScreens({QStringLiteral("DP-1")});
    }

    void cleanup()
    {
        // Note the shape: this teardown always clears the engine BEFORE the
        // adaptor dies, so the live-connection destruction order is the one
        // path these tests never exercise.
        m_adaptor->clearEngine();
        delete m_parent;
        m_parent = nullptr;
        m_adaptor = nullptr;
        delete m_engine;
        m_engine = nullptr;
    }

    // The XML promises an empty array for a screen with no strip or one that
    // is not scrolling. Each refusal below leaves through a DIFFERENT guard.
    void testVisibleStripJson_refusesForeignScreen()
    {
        // Owned, scrolling, but no windows yet: the "no strip" half of the
        // contract, reached past both guards.
        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("DP-1")), QStringLiteral("[]"));

        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);

        // The isActiveOnScreen ownership gate: the engine does not own this
        // screen, so its strip is not described.
        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("HDMI-2")), QStringLiteral("[]"));
        // The screenId.isEmpty() wire-boundary check. Boundary hygiene, not a
        // discriminating assertion: isActiveOnScreen would reject "" anyway,
        // so deleting the isEmpty check keeps this green. What it pins is the
        // documented answer for a malformed argument, which should not depend
        // on how the callee happens to treat it.
        QCOMPARE(m_adaptor->visibleStripJson(QString()), QStringLiteral("[]"));

        // Positive control: the screen the engine DOES own describes a strip,
        // so the refusals above are not just "this fixture has no rects".
        const QString payload = m_adaptor->visibleStripJson(QStringLiteral("DP-1"));
        QVERIFY(payload != QStringLiteral("[]"));
        // Compact JSON, as the D-Bus consumers assume: no indentation and no
        // separator whitespace anywhere in the payload.
        QVERIFY2(!payload.contains(QLatin1Char('\n')) && !payload.contains(QLatin1Char(' ')),
                 qPrintable(QStringLiteral("payload is not compact JSON: %1").arg(payload)));
    }

    // After the screen leaves scrolling mode the preview goes empty, and the
    // adaptor's isActiveOnScreen gate is what produces that. setActiveScreens
    // prunes only the screen's CURRENT (desktop, activity) key, so a strip
    // built on a sibling desktop survives the departure and still resolves.
    // The sibling-context test below is the one that pins it.
    void testVisibleStripJson_emptyAfterModeExit()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        QVERIFY(m_adaptor->visibleStripJson(QStringLiteral("DP-1")) != QStringLiteral("[]"));

        m_engine->setActiveScreens({});
        QVERIFY(!m_engine->isActiveOnScreen(QStringLiteral("DP-1")));
        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
    }

    // The gate's load-bearing case: the strip lives on a desktop the screen
    // was NOT on when it left the scrolling set, so the engine still has
    // rects to hand out. Only the adaptor's gate keeps the wire empty.
    // (engine_core.cpp setActiveScreens: "Prune ONLY the leaving screen's
    // CURRENT (desktop, activity) context".)
    void testVisibleStripJson_survivingSiblingContextStillRefused()
    {
        m_engine->setCurrentDesktop(1);
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        QVERIFY(m_adaptor->visibleStripJson(QStringLiteral("DP-1")) != QStringLiteral("[]"));

        // Leave the set while the screen sits on desktop 2 — desktop 1's
        // strip is a sibling context and is not pruned.
        m_engine->setCurrentDesktop(2);
        m_engine->setActiveScreens({});
        m_engine->setCurrentDesktop(1);

        QVERIFY(!m_engine->isActiveOnScreen(QStringLiteral("DP-1")));
        QVERIFY2(!m_engine->visibleTileRectsRelative(QStringLiteral("DP-1")).isEmpty(),
                 "sibling-context strip must survive the screen leaving the set, or this test proves nothing");
        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
    }

    // The documented payload: normalized rects, each carrying the tile's
    // 1-based visible slot in strip order. zoneNumber is the field a
    // consumer needs to map a rect back to a scroll zone, and it is the one
    // the DocString used to omit.
    void testVisibleStripJson_shapeAndZoneNumbers()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);

        // Field-mapping cross-check against the engine read the adaptor also
        // consumes: not an independent oracle, but it catches a transposed
        // or mislabelled field (r.y() written under "x"). Two rects because the
        // default column width is half the work area: 1200 / 600 = exactly
        // two columns in the viewport, so nothing is scrolled off yet.
        const QVector<QRectF> engineRects = m_engine->visibleTileRectsRelative(QStringLiteral("DP-1"));
        QCOMPARE(engineRects.size(), 2);

        const QJsonDocument doc = QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8());
        QVERIFY(doc.isArray());
        const QJsonArray arr = doc.array();
        QCOMPARE(arr.size(), engineRects.size());

        // Failures accumulate rather than aborting on the first bad rect, so
        // one failure does not hide the state of the others.
        QStringList failures;
        for (int i = 0; i < arr.size(); ++i) {
            collectRectMismatches(arr.at(i).toObject(), engineRects.at(i), i + 1, failures);
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));

        // Stack both windows into ONE column. What this pins is that the
        // numbering does not COLLAPSE when tiles share a column: each
        // visible window keeps its own distinct sequential number (per-column
        // ordinals were the old model; they rendered duplicate labels).
        // Focus the LEFT column first: consume pulls the neighbour to its
        // right into the active column, so with the last-opened window active
        // there is nothing to its right and the call is a no-op.
        m_engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("DP-1"));
        m_engine->consumeWindowIntoColumn(QStringLiteral("DP-1"));

        const QVector<ScrollEngine::VisibleTile> stackedTiles = m_engine->visibleTiles(QStringLiteral("DP-1"));
        QCOMPARE(stackedTiles.size(), 2);
        // Same column, asserted on the strip index rather than on a float x
        // that is legitimately 0.0 for the leftmost column.
        QCOMPARE(stackedTiles.at(0).columnIndex, stackedTiles.at(1).columnIndex);

        const QVector<QRectF> stackedRects = m_engine->visibleTileRectsRelative(QStringLiteral("DP-1"));
        QCOMPARE(stackedRects.size(), 2);

        const QJsonDocument stackedDoc =
            QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8());
        const QJsonArray stackedArr = stackedDoc.array();
        QCOMPARE(stackedArr.size(), stackedRects.size());

        // The stacked strip is the interesting ordering case, so it carries
        // the same rect cross-check as the unstacked one above.
        QStringList stackedFailures;
        for (int i = 0; i < stackedArr.size(); ++i) {
            collectRectMismatches(stackedArr.at(i).toObject(), stackedRects.at(i), i + 1, stackedFailures);
        }
        QVERIFY2(stackedFailures.isEmpty(), qPrintable(stackedFailures.join(QStringLiteral("; "))));

        // Against the ENGINE's numbers, not against the payload's own index.
        // The adaptor relays VisibleTile::zoneNumber, so comparing each entry
        // with the tile at the same position is what pins that it relays the
        // engine's numbering rather than re-deriving one; the ordinals-are-1-
        // and-2 claim below is the engine's contract, asserted separately.
        for (int i = 0; i < stackedArr.size(); ++i) {
            QCOMPARE(stackedArr.at(i).toObject().value(QLatin1String("zoneNumber")).toInt(-1),
                     stackedTiles.at(i).zoneNumber);
        }
        QCOMPARE(stackedTiles.at(0).zoneNumber, 1);
        QCOMPARE(stackedTiles.at(1).zoneNumber, 2);
    }

    // The screenId argument has to select the strip. With one owned screen
    // the whole parameter could be ignored and every test above would still
    // pass, so give the engine two screens with different strips.
    void testVisibleStripJson_perScreenPayloads()
    {
        m_engine->setActiveScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-2"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("DP-2"), 0, 0);

        // The fixture's geometry providers ignore screenId, so the two
        // payloads differ by TILE COUNT, not by rect values.
        const QJsonArray first =
            QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8()).array();
        const QJsonArray second =
            QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-2")).toUtf8()).array();
        QCOMPARE(first.size(), 1);
        QCOMPARE(second.size(), 2);
    }

    // The payload describes what is VISIBLE, not what is managed: a column
    // scrolled out of the viewport is parked off-canvas and must not appear.
    void testVisibleStripJson_excludesOffScreenColumns()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|c"), QStringLiteral("DP-1"), 0, 0);

        QCOMPARE(m_engine->managedWindowOrder(QStringLiteral("DP-1")).size(), 3);

        const QJsonArray arr =
            QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8()).array();
        // Track the engine's own visible set rather than a hardcoded count:
        // exactly how many columns survive the clip is a width/viewport
        // detail, but it must be fewer than the three managed windows.
        QCOMPARE(arr.size(), m_engine->visibleTiles(QStringLiteral("DP-1")).size());
        QVERIFY2(arr.size() < 3, "third column must be scrolled out, or this test does not exercise exclusion");

        // Zone numbers stay contiguous 1..N over what remains — the excluded
        // column must not leave a hole in the number space.
        for (int i = 0; i < arr.size(); ++i) {
            QCOMPARE(arr.at(i).toObject().value(QLatin1String("zoneNumber")).toInt(-1), i + 1);
        }
    }

    // The gap regime end to end: with an outer gap the engine clips its
    // tiles to an inset work area, and the payload must still mirror the
    // engine's gap-aware rects field-for-field. This cannot DETECT a dropped
    // inset on its own; what it pins is that the adaptor faithfully relays
    // the gap-regime output rather than a stale or differently-derived list.
    void testVisibleStripJson_relaysGapInsetTiles()
    {
        m_engine->setContextGapProvider([](const QString&) {
            return QVariantMap{{QString(PhosphorEngine::PerScreenKeys::OuterGap), 20}};
        });
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);

        const QVector<QRectF> engineRects = m_engine->visibleTileRectsRelative(QStringLiteral("DP-1"));
        QVERIFY(!engineRects.isEmpty());

        const QJsonArray arr =
            QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8()).array();
        QCOMPARE(arr.size(), engineRects.size());

        QStringList failures;
        for (int i = 0; i < arr.size(); ++i) {
            collectRectMismatches(arr.at(i).toObject(), engineRects.at(i), i + 1, failures);
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
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
        // Boundary hygiene, like the empty-screenId strip case: the ownership
        // gate rejects "" on its own, so this arm does not discriminate the
        // isEmpty check. It pins the documented answer, not the guard.
        m_adaptor->focusColumn(QString(), -1); // no screen at all
        m_adaptor->focusColumn(QStringLiteral("DP-1"), 0); // not a direction
        m_adaptor->focusColumn(QStringLiteral("DP-1"), 2); // not a direction either
        QCOMPARE(activateSpy.count(), 0);

        // Positive control: the same call with a real screen and a real
        // direction DOES move focus, so the refusals above discriminate.
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 1);
    }

    // Which WAY each delta goes, not just that something moved: -1 is left
    // and +1 is right, per the XML's DocString.
    void testFocusColumn_mapsDeltaToDirection()
    {
        QSignalSpy activateSpy(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);

        // An empty strip has nothing to activate, so a well-formed call is
        // still silent — the counts below start from a real zero.
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 0);

        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        activateSpy.clear();

        // app|b opened last and holds focus, so left lands on app|a.
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.at(0).at(0).toString(), QStringLiteral("app|a"));

        m_adaptor->focusColumn(QStringLiteral("DP-1"), 1);
        QCOMPARE(activateSpy.count(), 2);
        QCOMPARE(activateSpy.at(1).at(0).toString(), QStringLiteral("app|b"));
    }

    // The property is documented as sorted so a consumer can compare it with
    // a signal payload for the same set. The argument is a QSet, so the
    // braces below carry no order; the property must IMPOSE one. Five ids
    // make an unsorted read near-certain to differ from the expectation
    // (QSet iteration order is process-seeded, not sorted).
    void testScrollingScreens_sortedAcrossScreens()
    {
        m_engine->setActiveScreens({QStringLiteral("HDMI-3"), QStringLiteral("DP-2"), QStringLiteral("DP-1"),
                                    QStringLiteral("eDP-1"), QStringLiteral("DVI-4")});

        QCOMPARE(m_adaptor->scrollingScreens(),
                 (QStringList{QStringLiteral("DP-1"), QStringLiteral("DP-2"), QStringLiteral("DVI-4"),
                              QStringLiteral("HDMI-3"), QStringLiteral("eDP-1")}));
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

    // clearEngine also DISCONNECTS: the engine outlives the adaptor's
    // interest in it during shutdown, and a surviving connection would keep
    // pushing screen sets onto a bus interface that has stopped answering.
    void testClearedEngine_stopsRelayingSignals()
    {
        QSignalSpy spy(m_adaptor, &ScrollingAdaptor::scrollingScreensChanged);
        m_adaptor->clearEngine();

        m_engine->setActiveScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        QCOMPARE(spy.count(), 0);
    }

private:
    /// Append a description of every way @p obj fails to describe @p expected
    /// at 1-based visible slot @p slot. Accumulating instead of asserting
    /// keeps one bad rect from hiding the state of the rest.
    static void collectRectMismatches(const QJsonObject& obj, const QRectF& expected, int slot, QStringList& failures)
    {
        // Explicit epsilon rather than qFuzzyCompare: x and y are legitimately
        // 0.0 for the leftmost / topmost tile, and qFuzzyCompare is unusable
        // against zero.
        constexpr double kEpsilon = 1e-9;
        const struct
        {
            QLatin1String key;
            double want;
        } fields[] = {
            {QLatin1String("x"), expected.x()},
            {QLatin1String("y"), expected.y()},
            {QLatin1String("width"), expected.width()},
            {QLatin1String("height"), expected.height()},
        };
        for (const auto& field : fields) {
            if (!obj.contains(field.key)) {
                failures.append(QStringLiteral("rect %1: missing %2").arg(slot).arg(QString(field.key)));
                continue;
            }
            const double actual = obj.value(field.key).toDouble(-1.0);
            if (qAbs(actual - field.want) > kEpsilon) {
                failures.append(QStringLiteral("rect %1: %2 = %3, engine says %4")
                                    .arg(slot)
                                    .arg(QString(field.key))
                                    .arg(actual)
                                    .arg(field.want));
            }
            if (actual < 0.0 || actual > 1.0) {
                failures.append(
                    QStringLiteral("rect %1: %2 = %3 outside 0..1").arg(slot).arg(QString(field.key)).arg(actual));
            }
        }
        const int zoneNumber = obj.value(QLatin1String("zoneNumber")).toInt(-1);
        if (zoneNumber != slot) {
            failures.append(QStringLiteral("rect %1: zoneNumber %2 != slot %1").arg(slot).arg(zoneNumber));
        }
    }

    ScrollEngine* m_engine = nullptr;
    QObject* m_parent = nullptr;
    ScrollingAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestScrollingAdaptor)
#include "test_scrolling_adaptor.moc"
