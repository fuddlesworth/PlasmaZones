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
 *  6. A context outer-gap override reaches the wire: the payload's rects
 *     track the engine's own gap-inset tiles rather than an ungapped
 *     recomputation.
 *  7. presetVocabularyJson carries the same ownership gates, and its payload
 *     mixes the two vocabularies independently: a widths-only template
 *     override replaces the widths and leaves the heights on the fallback.
 *  8. scrollingScreens answers sorted, across several screens.
 *  9. scrollingScreensChanged is gated on an actual change: the engine
 *     re-announces an identical set on every desktop switch, and a wire
 *     consumer comparing successive payloads must not see a phantom one.
 * 10. clearEngine leaves every slot answering safely AND stops relaying.
 * 11. The scrollingScreens property never emits PropertiesChanged (change
 *     traffic rides the dedicated signal only).
 * 12. clearWindowedFullscreen refuses an empty id, an unknown window and an
 *     unflagged tile as silent no-ops, and clears a genuinely flagged tile
 *     exactly once (placementChanged discriminates: every refusal returns
 *     before emitting). The null-engine arm rides item 10's clearEngine
 *     sweep like every other slot.
 * 13. reapplyWindowGeometry refuses an empty and an unknown id silently,
 *     and its evict makes a rect-stable relayout re-emit — with a control
 *     first proving that same relayout is silent WITHOUT the evict. Its
 *     null-engine arm rides the item 10 sweep too.
 * 14. The four absolute setters share focusColumn's ownership gate, refuse
 *     out-of-range and non-finite values silently (inclusive at both bounds),
 *     and write the intent kind each form documents — width proportion
 *     exact, width/height px Fixed, height proportion a Preset anchor that
 *     relayout snaps to the height vocabulary.
 * 15. blueprintProgressJson carries the same ownership gates, answers zeroes
 *     (not an empty object) for an owned screen with no blueprint, and its
 *     `used` counter SPENDS: it does not fall back when a column closes.
 * 16. A tabbed column's tile carries the tab-indicator keys and an untabbed
 *     one carries none at all — the only thing on the wire distinguishing a
 *     stack of tabs from the single window the walk emits for it.
 * 17. stripChanged relays the engine's placement changes as a wake-up for
 *     anyone rendering the strip, naming the screen that changed and gated on
 *     the screens the engine owns. Nothing reaches the bus after clearEngine
 *     either, though that one is the relay's own null guard rather than the
 *     disconnect — see the note in testClearedEngine_stopsRelayingSignals.
 */

#include <QTest>
#include <QSignalSpy>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <limits>

#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

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
        // outside 0..1). The two rects also differ, but which basis the
        // payload normalizes against is NOT pinned here: every assertion in
        // this file cross-checks the wire payload against the engine's own
        // relative rects, so a swap to the available geometry would normalize
        // against 760 instead of 800, stay inside 0..1, and agree with the
        // engine either way. Known coverage gap, kept because an absolute
        // expected value would have to be derived from a live run.
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

        // Cross-check against the ENGINE's numbers. Under this fixture it is
        // redundant with collectRectMismatches' own tail (the engine stamps
        // 1..N contiguously by construction, so a re-deriving adaptor would
        // pass both) — kept as a cheap alignment pin, not as proof the
        // adaptor relays rather than re-derives; no legal fixture can
        // distinguish those while visibleTiles numbers contiguously.
        for (int i = 0; i < stackedArr.size(); ++i) {
            QCOMPARE(stackedArr.at(i).toObject().value(QLatin1String("zoneNumber")).toInt(-1),
                     stackedTiles.at(i).zoneNumber);
        }
        QCOMPARE(stackedTiles.at(0).zoneNumber, 1);
        QCOMPARE(stackedTiles.at(1).zoneNumber, 2);
    }

    // A tabbed column's payload has to say so. The walk emits only the SHOWN
    // tab, so without these keys the wire describes a five-window stack
    // exactly as it describes a single window, and the settings app's strip
    // thumbnail draws them identically.
    void testVisibleStripJson_tabIndicatorKeys()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("DP-1"));
        m_engine->consumeWindowIntoColumn(QStringLiteral("DP-1"));

        // Stacked but NOT tabbed: no tab key on either element. The absent
        // key is the contract, not a zero — the settings app forwards only
        // what is present, so a stray zeroed triple here would be indis-
        // tinguishable from a daemon that genuinely has no tab data.
        const QJsonArray plain =
            QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8()).array();
        QCOMPARE(plain.size(), 2);
        for (const QJsonValue& value : plain) {
            QVERIFY(!value.toObject().contains(QLatin1String("tabCount")));
            QVERIFY(!value.toObject().contains(QLatin1String("activeTab")));
            QVERIFY(!value.toObject().contains(QLatin1String("tabPosition")));
            QVERIFY(!value.toObject().contains(QLatin1String("tabLength")));
        }

        m_engine->toggleColumnTabbed(QStringLiteral("DP-1"));

        const QVector<ScrollEngine::VisibleTile> tiles = m_engine->visibleTiles(QStringLiteral("DP-1"));
        QCOMPARE(tiles.size(), 1);
        const QJsonArray tabbed =
            QJsonDocument::fromJson(m_adaptor->visibleStripJson(QStringLiteral("DP-1")).toUtf8()).array();
        QCOMPARE(tabbed.size(), 1);
        const QJsonObject obj = tabbed.at(0).toObject();
        // Relayed off the tile, cross-checked field by field against the
        // engine read the adaptor consumes — the same shape as the rect
        // cross-check above, and it catches a transposed count/index pair.
        QCOMPARE(obj.value(QLatin1String("tabCount")).toInt(-1), tiles.at(0).tabCount);
        QCOMPARE(obj.value(QLatin1String("activeTab")).toInt(-1), tiles.at(0).activeTabIndex);
        QCOMPARE(obj.value(QLatin1String("tabPosition")).toInt(-1), static_cast<int>(tiles.at(0).tabIndicatorPosition));
        QCOMPARE(obj.value(QLatin1String("tabLength")).toDouble(-1.0), tiles.at(0).tabLengthProportion);
        // Both tabs counted, hidden one included: the pills stand for the
        // stack, not for what is on screen.
        QCOMPARE(obj.value(QLatin1String("tabCount")).toInt(-1), 2);
        QVERIFY(obj.value(QLatin1String("tabLength")).toDouble(-1.0) > 0.0);
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

    // The absolute setters: focusColumn's ownership gate, silent range
    // refusal against the ConfigDefaults bounds, and the intent each form
    // actually writes (width proportion exact, width/height px Fixed, height
    // proportion a Preset fraction anchor).
    void testAbsoluteSetters_gateValidateAndApply()
    {
        using PhosphorScrollEngine::ColumnWidth;
        using PhosphorScrollEngine::WindowHeight;
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        auto* state = static_cast<PhosphorScrollEngine::ScrollState*>(m_engine->stateForScreen(QStringLiteral("DP-1")));
        QVERIFY(state);
        const auto activeColumn = [state]() -> const PhosphorScrollEngine::Column& {
            return state->strip().columns().at(state->strip().activeColumnIndex());
        };

        // Refusals: foreign screen, empty screen id (the file's boundary
        // convention), below the proportion floor, above the ceiling, just
        // outside each inclusive bound, and NaN (D-Bus type 'd' carries it,
        // and an exclusion-form range test would wave it through) — none of
        // them may disturb the default width intent. Full-value compare, not
        // kind-only: a refusal that clamped instead of refusing would keep
        // the kind while corrupting the value.
        const ColumnWidth before = activeColumn().width;
        m_adaptor->setColumnWidthProportion(QStringLiteral("HDMI-2"), 0.25);
        m_adaptor->setColumnWidthProportion(QString(), 0.25);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.01);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 1.5);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.049);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 1.001);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(activeColumn().width, before);

        // The bounds themselves are ACCEPTED (inclusive range): a `<` flipped
        // to `<=` at either edge fails here.
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.05);
        QCOMPARE(activeColumn().width.kind, ColumnWidth::Kind::Proportion);
        QCOMPARE(activeColumn().width.proportion, 0.05);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 1.0);
        QCOMPARE(activeColumn().width.proportion, 1.0);

        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.25);
        QCOMPARE(activeColumn().width.kind, ColumnWidth::Kind::Proportion);
        QCOMPARE(activeColumn().width.proportion, 0.25);

        // Pixel form: out-of-range (including just-outside) refused with the
        // full intent untouched; the bounds accepted; in-range writes Fixed.
        // The foreign-screen arm is exercised on EVERY setter, not just the
        // proportion one: the ownership gate is per-method code, and deleting
        // it from one leaves the suite green if only a sibling pins it.
        const ColumnWidth beforePx = activeColumn().width;
        m_adaptor->setColumnWidthPixels(QStringLiteral("HDMI-2"), 640);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 50);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 99);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 10001);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 20000);
        m_adaptor->setColumnWidthPixels(QString(), 640);
        QCOMPARE(activeColumn().width, beforePx);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 100);
        QCOMPARE(activeColumn().width.kind, ColumnWidth::Kind::Fixed);
        QCOMPARE(activeColumn().width.fixedPx, 100);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 10000);
        QCOMPARE(activeColumn().width.fixedPx, 10000);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 640);
        QCOMPARE(activeColumn().width.kind, ColumnWidth::Kind::Fixed);
        QCOMPARE(activeColumn().width.fixedPx, 640);

        // Height twins: px is Fixed, proportion is the Preset fraction
        // anchor (heights have no exact-proportion kind).
        const auto activeHeight = [&activeColumn]() -> const WindowHeight& {
            const PhosphorScrollEngine::Column& col = activeColumn();
            return col.tiles.at(col.activeTileIdx).height;
        };
        // Foreign-screen refusal + the same bound pins as the width twin: a
        // `<` flipped to `<=` at either height bound was invisible before.
        // Full-value compare, like the width arm and the proportion arm
        // below: the starting kind happens to differ from the kind a clamp
        // would write, so a kind-only check passes here today, but it stops
        // discriminating the moment this block is reordered after one of the
        // legs that leaves the height Fixed.
        const WindowHeight beforePxRefusals = activeHeight();
        m_adaptor->setWindowHeightPixels(QStringLiteral("HDMI-2"), 300);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 50);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 99);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 10001);
        m_adaptor->setWindowHeightPixels(QString(), 300);
        QCOMPARE(activeHeight(), beforePxRefusals);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Auto);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 100);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Fixed);
        QCOMPARE(activeHeight().fixedPx, 100);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 10000);
        QCOMPARE(activeHeight().fixedPx, 10000);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 300);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Fixed);
        QCOMPARE(activeHeight().fixedPx, 300);
        // Height-proportion refusals, full-value compare like the width arm
        // (kind-only would miss a clamp that rewrote fixedPx), with the
        // foreign screen, both out-of-range sides and NaN covered.
        const WindowHeight beforeHeightRefusals = activeHeight();
        m_adaptor->setWindowHeightProportion(QStringLiteral("HDMI-2"), 0.5);
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 0.01);
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 1.5);
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), std::numeric_limits<double>::quiet_NaN());
        m_adaptor->setWindowHeightProportion(QString(), 0.5);
        QCOMPARE(activeHeight(), beforeHeightRefusals);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Fixed);
        // An OFF-VOCABULARY fraction is accepted and stored VERBATIM as the
        // anchor — the setter is not exact and does not snap at store time;
        // relayout snaps the anchor to the nearest effective height preset,
        // per the XML DocString. 0.42 sits between the default vocabulary's
        // 1/3 and 1/2 entries, so a setter that stored the nearest entry
        // instead of the anchor would fail the exact compare below.
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 0.42);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Preset);
        QCOMPARE(activeHeight().presetFraction, 0.42);
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 0.5);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Preset);
        QCOMPARE(activeHeight().presetFraction, 0.5);
    }

    // Same ownership gates as focusColumn, plus the payload's mixed-vocabulary
    // shape: the two preset lists are overridden independently.
    void testPresetVocabularyJson_gatesAndShape()
    {
        // Foreign and empty screen ids answer the empty object.
        QCOMPARE(m_adaptor->presetVocabularyJson(QStringLiteral("HDMI-2")), QStringLiteral("{}"));
        QCOMPARE(m_adaptor->presetVocabularyJson(QString()), QStringLiteral("{}"));

        // Capture the fallback heights BEFORE the template push, so the
        // mixed-vocabulary assertion below can pin that the heights are
        // byte-identical to the fallback rather than merely non-empty.
        const QJsonObject before =
            QJsonDocument::fromJson(m_adaptor->presetVocabularyJson(QStringLiteral("DP-1")).toUtf8()).object();
        const QJsonArray fallbackHeights = before.value(QLatin1String("windowHeights")).toArray();
        QVERIFY(!fallbackHeights.isEmpty());

        // The owned screen answers both lists. With a widths-only template
        // override pushed, the widths are the template's and the heights
        // stay on the fallback (mixed vocabulary).
        QVariantMap templ;
        templ.insert(PhosphorScrollEngine::ScrollPerScreenKeys::presetColumnWidths(), QVariantList{0.2, 0.8});
        m_engine->applyPerScreenConfig(QStringLiteral("DP-1"), templ);

        const QString payload = m_adaptor->presetVocabularyJson(QStringLiteral("DP-1"));
        const QJsonObject obj = QJsonDocument::fromJson(payload.toUtf8()).object();
        const QJsonArray widths = obj.value(QLatin1String("columnWidths")).toArray();
        QCOMPARE(widths.size(), 2);
        QCOMPARE(widths.at(0).toDouble(), 0.2);
        QCOMPARE(widths.at(1).toDouble(), 0.8);
        QCOMPARE(obj.value(QLatin1String("windowHeights")).toArray(), fallbackHeights);
    }

    // The blueprint readout carries focusColumn's ownership gates, and its
    // counter SPENDS rather than tracking the live column count: the whole
    // point of the wire field is to show that a closed column does not give
    // its starting column back.
    void testBlueprintProgressJson_gatesAndSpends()
    {
        // Foreign and empty screen ids answer the empty object.
        QCOMPARE(m_adaptor->blueprintProgressJson(QStringLiteral("HDMI-2")), QStringLiteral("{}"));
        QCOMPARE(m_adaptor->blueprintProgressJson(QString()), QStringLiteral("{}"));

        const auto progress = [this]() {
            const QString payload = m_adaptor->blueprintProgressJson(QStringLiteral("DP-1"));
            return QJsonDocument::fromJson(payload.toUtf8()).object();
        };

        // An owned screen with NO blueprint answers zeroes rather than an
        // empty object: the gate above is about ownership, not content.
        // Compared as the PAYLOAD, not through the parsed object: an empty
        // object's "total" also reads 0, so a regression that answered "{}"
        // here would have satisfied a value compare while destroying the very
        // distinction the caller branches on (getScrollingBlueprintProgress
        // returns an empty map for "nothing to describe").
        QCOMPARE(m_adaptor->blueprintProgressJson(QStringLiteral("DP-1")), QStringLiteral("{\"total\":0,\"used\":0}"));

        QVariantList blueprint;
        for (const qreal width : {0.6, 0.4}) {
            QVariantMap entry;
            entry.insert(PhosphorScrollEngine::ScrollPerScreenKeys::templateColumnWidth(), width);
            blueprint.append(entry);
        }
        QVariantMap templ;
        templ.insert(PhosphorScrollEngine::ScrollPerScreenKeys::templateColumns(), blueprint);
        m_engine->applyPerScreenConfig(QStringLiteral("DP-1"), templ);

        QCOMPARE(progress().value(QLatin1String("total")).toInt(), 2);
        QCOMPARE(progress().value(QLatin1String("used")).toInt(), 0);

        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        QCOMPARE(progress().value(QLatin1String("used")).toInt(), 1);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        QCOMPARE(progress().value(QLatin1String("used")).toInt(), 2);

        // The load-bearing leg: one column closes and `used` STAYS at 2. A
        // readout derived from the column count would drop back to 1 and
        // promise a starting column the engine will never hand out again.
        m_engine->windowClosed(QStringLiteral("app|b"));
        QCoreApplication::processEvents();
        QCOMPARE(progress().value(QLatin1String("used")).toInt(), 2);
        QCOMPARE(progress().value(QLatin1String("total")).toInt(), 2);
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
        // Non-empty BEFORE the clear, so the emptiness asserted afterwards is
        // this call's doing rather than a fixture that never populated it.
        QVERIFY(!m_adaptor->scrollingScreens().isEmpty());
        m_adaptor->clearEngine();

        QCOMPARE(m_adaptor->visibleStripJson(QStringLiteral("DP-1")), QStringLiteral("[]"));
        QCOMPARE(m_adaptor->presetVocabularyJson(QStringLiteral("DP-1")), QStringLiteral("{}"));
        // The blueprint reader has the same `!m_engine` conjunct and belongs
        // in the same sweep — without it, dropping that conjunct is a null
        // dereference on the shutdown path with nothing failing.
        QCOMPARE(m_adaptor->blueprintProgressJson(QStringLiteral("DP-1")), QStringLiteral("{}"));
        QVERIFY(m_adaptor->scrollingScreens().isEmpty());
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1); // must not crash
        // The four setters open with the same `!m_engine` guard — this test
        // is the shutdown path that guard exists for.
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.5);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 640);
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 0.5);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 300);
        m_adaptor->clearWindowedFullscreen(QStringLiteral("app|a")); // must not crash
        m_adaptor->reapplyWindowGeometry(QStringLiteral("app|a")); // must not crash
    }

    // clearEngine also DISCONNECTS: the engine outlives the adaptor's
    // interest in it during shutdown, and a surviving connection would keep
    // pushing screen sets onto a bus interface that has stopped answering.
    void testClearedEngine_stopsRelayingSignals()
    {
        QSignalSpy spy(m_adaptor, &ScrollingAdaptor::scrollingScreensChanged);
        // The strip wake-up rides the same sweep, but be clear about what this
        // spy can and cannot show: its relay lambda tests `!m_engine` before
        // touching the engine, so a surviving connection would be inert and
        // this assertion holds with the disconnect deleted. The screen-set spy
        // above is the discriminating one — that lambda has no such guard, and
        // clearEngine also clears the last-broadcast set it compares against.
        QSignalSpy stripSpy(m_adaptor, &ScrollingAdaptor::stripChanged);
        m_adaptor->clearEngine();

        m_engine->setActiveScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(stripSpy.count(), 0);
    }

    // The strip wake-up the Monitors page's thumbnail subscribes to. What it
    // pins is the RELAY and its ownership gate, not a payload: the signal
    // deliberately carries none and is not compared against the strip (see
    // its declaration), so "fires on a change" and "names the right screen"
    // are the whole contract.
    void testStripChanged_wakesForOwnedScreensOnly()
    {
        // TWO owned screens, so the id the relay forwards is load-bearing.
        // With only one, an implementation that ignored its argument and
        // named the sole owned screen would pass every assertion below.
        m_engine->setActiveScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        QSignalSpy spy(m_adaptor, &ScrollingAdaptor::stripChanged);

        // Opening a window is a placement change on an owned screen.
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toString(), QStringLiteral("DP-1"));

        // The other owned screen names itself, not the first one.
        spy.clear();
        m_engine->windowOpened(QStringLiteral("app|d"), QStringLiteral("DP-2"), 0, 0);
        QVERIFY(spy.count() >= 1);
        QCOMPARE(spy.last().at(0).toString(), QStringLiteral("DP-2"));

        // A tab switch is the case the poll could not see quickly: it moves
        // which tile the strip shows without moving a single rect.
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("DP-1"));
        m_engine->consumeWindowIntoColumn(QStringLiteral("DP-1"));
        m_engine->toggleColumnTabbed(QStringLiteral("DP-1"));
        // The consume left focus on the pulled-in tile, so the column shows
        // its LAST tab. Asserted rather than assumed: switching to the tab
        // that is already shown is a no-op verb that emits nothing, and this
        // test would then pass without a tab switch ever happening.
        QCOMPARE(m_engine->visibleTiles(QStringLiteral("DP-1")).first().activeTabIndex, 1);
        spy.clear();
        // Through the VERB, which is the deterministic trigger. The other
        // route to the same change — a windowFocused report from the
        // compositor — reaches the same emit through
        // ScrollStrip::focusWindow, so this arm covers both; what neither
        // route emits is a wake-up for a focus that moves nothing, which is
        // the emit-on-change rule and not a gap.
        // The premise, enforced rather than asserted in prose: this really is
        // a change no rect-watching poll could have seen. Captured either side
        // of the verb — if a future relayout DID move a rect here, the case
        // would quietly stop covering what it was written for.
        const QVector<QRect> rectsBefore = m_engine->visibleTileRects(QStringLiteral("DP-1"));
        m_engine->focusWindowTop(QStringLiteral("DP-1"));
        QCOMPARE(m_engine->visibleTiles(QStringLiteral("DP-1")).first().activeTabIndex, 0);
        QCOMPARE(m_engine->visibleTileRects(QStringLiteral("DP-1")), rectsBefore);
        QVERIFY2(spy.count() >= 1, "a tab switch must wake the preview: no rect moves, so nothing else would");
    }

    // The ownership gate on the relay, on its own. A released screen answers
    // "[]" from visibleStripJson, so a wake-up naming it could only ever say
    // "nothing". Whether any production path emits a placement change for a
    // released screen is NOT established here — this drives the signal itself
    // — so read this as "if one ever does, the gate is what stops it".
    void testStripChanged_staysSilentForAReleasedScreen()
    {
        m_engine->setActiveScreens({QStringLiteral("DP-2")});
        QSignalSpy spy(m_adaptor, &ScrollingAdaptor::stripChanged);

        // Driven at the engine's own signal rather than through a verb: every
        // verb refuses a screen outside the scrolling set before it reaches an
        // emit, so a verb-driven leg proves only that the ENGINE declined —
        // it never reaches the adaptor's gate at all, and passes just as well
        // with that gate deleted.
        Q_EMIT m_engine->placementChanged(QStringLiteral("DP-1"));
        QCOMPARE(spy.count(), 0);

        // Positive control: the same emit for an OWNED screen does surface,
        // so the silence above is the gate and not a dead signal path.
        Q_EMIT m_engine->placementChanged(QStringLiteral("DP-2"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).toString(), QStringLiteral("DP-2"));

        // An empty screen id surfaces nothing either. Documents the outcome
        // rather than isolating the relay's `isEmpty()` conjunct: the active
        // screen set never holds an empty string, so the ownership lookup
        // already answers false and deleting that conjunct keeps this green.
        Q_EMIT m_engine->placementChanged(QString());
        QCOMPARE(spy.count(), 1);
    }

    // The scrollingScreens property's DocString promises that changes are
    // announced on scrollingScreensChanged and NOT through
    // org.freedesktop.DBus.Properties.PropertiesChanged. The property does
    // carry a NOTIFY, which is what makes the claim worth pinning: if QtDBus
    // ever relayed that NOTIFY into PropertiesChanged, the XML would be
    // telling consumers to ignore a signal they were in fact receiving, and a
    // consumer that believed it would poll instead of subscribing.
    //
    // Driven over a REAL bus, because the claim is about what reaches the wire
    // rather than about the adaptor's own emissions.
    void testScrollingScreensProperty_doesNotEmitPropertiesChanged()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY2(bus.isConnected(), "this suite runs under a private bus; see phosphor_apply_test_isolation");
        const QString path = QStringLiteral("/PropertiesChangedProbe");
        QVERIFY(bus.registerObject(path, m_parent, QDBusConnection::ExportAllContents));

        QDBusMessage changed;
        QVERIFY(bus.connect(bus.baseService(), path, QStringLiteral("org.freedesktop.DBus.Properties"),
                            QStringLiteral("PropertiesChanged"), this, SLOT(onPropertiesChanged(QDBusMessage))));

        m_propertiesChangedCount = 0;
        QSignalSpy own(m_adaptor, &ScrollingAdaptor::scrollingScreensChanged);

        m_engine->setActiveScreens({QStringLiteral("DP-1"), QStringLiteral("DP-2")});
        // Emitted synchronously through the engine connection, so it is
        // already counted rather than something to wait for.
        QVERIFY2(own.count() > 0, "the adaptor's own change signal must fire");
        // A relayed PropertiesChanged would arrive over the bus, which is
        // asynchronous — give it a window rather than concluding from the
        // same turn that emitted the signal above.
        //
        // Known limitation of asserting an ABSENCE on a wall clock: a loaded
        // machine that merely delayed the relay past this window passes too.
        // There is no positive control available (the whole claim is that
        // nothing is emitted on this path), so the window is the guarantee.
        QTest::qWait(200);

        QCOMPARE(m_propertiesChangedCount, 0);
        // Pair the match rule with the unregister: the test QObject lives
        // for the whole binary, so an undisconnected rule would outlive the
        // probe (hygiene only — nothing later reads the counter).
        bus.disconnect(bus.baseService(), path, QStringLiteral("org.freedesktop.DBus.Properties"),
                       QStringLiteral("PropertiesChanged"), this, SLOT(onPropertiesChanged(QDBusMessage)));
        bus.unregisterObject(path);
    }

    // clearWindowedFullscreen's wire-boundary policy: every refusal (empty
    // id, unknown window, unflagged tile) is a silent no-op, and a genuine
    // clear reaches the strip. placementChanged discriminates — the engine
    // returns before emitting on every refusal path, and emits on success.
    void testClearWindowedFullscreen_gatesAndClears()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("DP-1"));
        m_engine->toggleWindowedFullscreen(QStringLiteral("DP-1"));

        QSignalSpy placement(m_engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

        m_adaptor->clearWindowedFullscreen(QString()); // empty id
        m_adaptor->clearWindowedFullscreen(QStringLiteral("nobody|9")); // unknown window
        m_adaptor->clearWindowedFullscreen(QStringLiteral("app|b")); // tracked but unflagged
        QCOMPARE(placement.count(), 0);

        // Positive control: the flagged tile clears, once — a second call
        // finds the flag already down and refuses silently again.
        m_adaptor->clearWindowedFullscreen(QStringLiteral("app|a"));
        QCOMPARE(placement.count(), 1);
        m_adaptor->clearWindowedFullscreen(QStringLiteral("app|a"));
        QCOMPARE(placement.count(), 1);
    }

    // reapplyWindowGeometry shares clearWindowedFullscreen's wire-boundary
    // policy, and its whole point is defeating the emit-on-change gate: a
    // relayout whose rects never moved is normally silent, and the evict
    // must make that same relayout re-emit the batch (the compositor moved
    // the window behind the engine's back, so the gate's memory is stale).
    void testReapplyWindowGeometry_gatesAndReemits()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);

        QSignalSpy tiled(m_engine, &ScrollEngine::windowsTiled);

        m_adaptor->reapplyWindowGeometry(QString()); // empty id
        m_adaptor->reapplyWindowGeometry(QStringLiteral("nobody|9")); // unknown window
        QCOMPARE(tiled.count(), 0);

        // Silent control FIRST: a plain relayout with unchanged rects emits
        // nothing, so the re-emission below is attributable to the evicted
        // gate memory and not to the relayout itself.
        m_engine->retile(QStringLiteral("DP-1"));
        QCOMPARE(tiled.count(), 0);

        // Positive case: nothing in the strip moved, so only the evicted
        // gate memory explains the re-emission — and the batch must actually
        // CONTAIN the re-applied window (a count alone would pass a
        // regression that evicted the memory but emitted an unrelated
        // batch).
        m_adaptor->reapplyWindowGeometry(QStringLiteral("app|a"));
        QCOMPARE(tiled.count(), 1);
        bool sawA = false;
        const QJsonArray reBatch = QJsonDocument::fromJson(tiled.last().at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : reBatch) {
            if (v.toObject().value(QLatin1String("windowId")).toString() == QLatin1String("app|a")) {
                sawA = true;
            }
        }
        QVERIFY2(sawA, "the re-emitted batch must carry the re-applied window");
    }

public Q_SLOTS:
    void onPropertiesChanged(const QDBusMessage& msg)
    {
        if (msg.arguments().value(0).toString() == QLatin1String("org.plasmazones.Scrolling")) {
            ++m_propertiesChangedCount;
        }
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
    int m_propertiesChangedCount = 0;
};

QTEST_GUILESS_MAIN(TestScrollingAdaptor)
#include "test_scrolling_adaptor.moc"
