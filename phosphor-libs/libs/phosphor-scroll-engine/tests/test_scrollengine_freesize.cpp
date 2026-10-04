// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The scroll arm of the #1106 free-size restore, beyond the sibling case the
// behaviour suite pins: the lineage gate, the oversized and migration
// refusals, the move-or-size exclusivity of the own-record restore, the
// screen-containment gate on the move, the emit order the effect's float
// handler depends on, the cross-screen claim declining a float, and the
// column rect a float keeps as the float-back guard.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"
#include "scrollstubtracking.h"

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorIdentity/WindowId.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QtTest>

using namespace PhosphorScrollEngine;
using namespace ScrollTestUtils;

namespace {
const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kApp = QStringLiteral("app");
} // namespace

class TestScrollEngineFreeSize : public QObject
{
    Q_OBJECT

    // Owned by the fixture: the probe captures it by reference.
    QSet<QString> m_live;

    struct Rig
    {
        QObject owner;
        StubScrollSettings* settings = nullptr;
        StubWindowTracking* tracker = nullptr;
        ScrollEngine* engine = nullptr;
    };

    void build(Rig& rig)
    {
        rig.settings = new StubScrollSettings(&rig.owner);
        rig.tracker = new StubWindowTracking(&rig.owner);
        rig.tracker->placementStore().setLiveInstanceProbe([this](const QString& windowId) {
            return m_live.contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        });
        rig.engine = makeProviderEngine(&rig.owner, {kS1, kS2}, {}, {}, rig.tracker);
        rig.engine->setEngineSettings(rig.settings);
        rig.engine->refreshConfigFromSettings();
    }

    static PhosphorEngine::WindowPlacement record(const ScrollEngine* engine, const QString& windowId,
                                                  const QString& state, const QString& screen, const QRect& freeRect)
    {
        PhosphorEngine::WindowPlacement p;
        p.windowId = windowId;
        p.appId = kApp;
        p.screenId = screen;
        PhosphorEngine::EngineSlot slot;
        slot.state = state;
        p.engines.insert(engine->engineId(), slot);
        if (freeRect.isValid()) {
            p.freeGeometryByScreen.insert(screen, freeRect);
        }
        return p;
    }

    static ScrollState* stateOn(ScrollEngine* engine, const QString& screen)
    {
        return static_cast<ScrollState*>(engine->stateForScreen(screen));
    }

    static void floatOnly(ScrollEngine* engine, const QString& windowId)
    {
        engine->setFloatPredicate([windowId](const QString& id, const QString&) {
            return id == windowId;
        });
    }

private Q_SLOTS:
    void initTestCase()
    {
        // The out-of-band half of the axis vacuity guard: pse_add_test runs
        // this binary twice and fails the vertical arm on a printed
        // "axis=horizontal", which only works if the suite prints the axis it
        // actually resolved.
        AX_GUARD_SUITE();
    }

    void init()
    {
        m_live.clear();
    }

    // A record under the opening uuid WITH a scroll slot is a placement by
    // a previous daemon lineage: the window is on screen already, and a
    // re-announce (restart, mode swap) must not resize it. A slot-less stub
    // under the same uuid is what the effect writes for every open on a
    // tiling screen, and is NOT a placement: the open is still a first one.
    void lineageGate_slotMeansPlaced_stubDoesNot()
    {
        Rig rig;
        build(rig);
        // A tiled live sibling supplies the size for the stub case.
        const QRect siblingFree(100, 100, 640, 400);
        QVERIFY(rig.tracker->placementStore().record(record(
            rig.engine, QStringLiteral("app|first"), PhosphorEngine::WindowPlacement::stateTiled(), kS1, siblingFree)));
        m_live.insert(QStringLiteral("first"));
        rig.engine->windowOpened(QStringLiteral("app|first"), kS1, 0, 0);

        QSignalSpy sizeSpy(rig.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);

        // Re-announce of a window this lineage placed: tiled slot under its
        // own uuid, floated by rule at the re-announce.
        QVERIFY(rig.tracker->placementStore().record(record(
            rig.engine, QStringLiteral("app|placed"), PhosphorEngine::WindowPlacement::stateTiled(), kS1, QRect())));
        m_live.insert(QStringLiteral("placed"));
        floatOnly(rig.engine, QStringLiteral("app|placed"));
        rig.engine->windowOpened(QStringLiteral("app|placed"), kS1, 0, 0);
        QVERIFY(stateOn(rig.engine, kS1)->isFloating(QStringLiteral("app|placed")));
        QCOMPARE(sizeSpy.count(), 0);

        // A slot-less stub under the opener's uuid: first placement, and the
        // stub is not a size source either (no slot), so the sibling's rect
        // is what comes back.
        PhosphorEngine::WindowPlacement stub;
        stub.windowId = QStringLiteral("app|fresh");
        stub.appId = kApp;
        stub.screenId = kS1;
        stub.freeGeometryByScreen.insert(kS1, QRect(0, 0, 999, 999));
        QVERIFY(rig.tracker->placementStore().record(stub));
        m_live.insert(QStringLiteral("fresh"));
        floatOnly(rig.engine, QStringLiteral("app|fresh"));
        rig.engine->windowOpened(QStringLiteral("app|fresh"), kS1, 0, 0);
        QVERIFY(stateOn(rig.engine, kS1)->isFloating(QStringLiteral("app|fresh")));
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), siblingFree.size());
    }

    // An oversized window floats because its minimum exceeds the work area:
    // the clamp would ask for less than that minimum, so no size is sent.
    void oversizedFloat_getsNoSize()
    {
        Rig rig;
        build(rig);
        const QRect siblingFree(100, 100, 640, 400);
        QVERIFY(rig.tracker->placementStore().record(record(
            rig.engine, QStringLiteral("app|first"), PhosphorEngine::WindowPlacement::stateTiled(), kS1, siblingFree)));
        m_live.insert(QStringLiteral("first"));
        rig.engine->windowOpened(QStringLiteral("app|first"), kS1, 0, 0);

        QSignalSpy sizeSpy(rig.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);
        const QRect work = defaultScreenRect();
        rig.engine->windowOpened(QStringLiteral("app|huge"), kS1, work.width() + 1, 0);
        QVERIFY(stateOn(rig.engine, kS1)->isFloating(QStringLiteral("app|huge")));
        QCOMPARE(sizeSpy.count(), 0);
    }

    // The own-record restore moves OR sizes, never both: with the position
    // restore opted in the full rect goes out and no size-only request
    // follows; with it declined the size comes from the record just re-bound.
    // A rect the containment gate refuses (keyed to this screen, lying
    // elsewhere) is neither moved to nor used as a size.
    void ownFloatingRecord_moveOrSizeNeverBoth()
    {
        Rig rig;
        build(rig);
        const QRect ownFree(40, 40, 700, 500);
        QSignalSpy geoSpy(rig.engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy sizeSpy(rig.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);

        // Move opted in (predicate unset): full rect, no size.
        QVERIFY(rig.tracker->placementStore().record(record(
            rig.engine, QStringLiteral("app|a"), PhosphorEngine::WindowPlacement::stateFloating(), kS1, ownFree)));
        rig.engine->windowOpened(QStringLiteral("app|a2"), kS1, 0, 0);
        QVERIFY(stateOn(rig.engine, kS1)->isFloating(QStringLiteral("app|a2")));
        QCOMPARE(geoSpy.count(), 1);
        QCOMPARE(sizeSpy.count(), 0);
        geoSpy.clear();
        rig.engine->windowClosed(QStringLiteral("app|a2"));

        // Move declined: size only.
        rig.engine->setRestorePositionPredicate([](const QString&) {
            return false;
        });
        QVERIFY(rig.tracker->placementStore().record(record(
            rig.engine, QStringLiteral("app|b"), PhosphorEngine::WindowPlacement::stateFloating(), kS1, ownFree)));
        rig.engine->windowOpened(QStringLiteral("app|b2"), kS1, 0, 0);
        QVERIFY(stateOn(rig.engine, kS1)->isFloating(QStringLiteral("app|b2")));
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), ownFree.size());
        rig.engine->windowClosed(QStringLiteral("app|b2"));

        // Move opted in but the rect does not lie on this screen: the
        // containment gate refuses it for the move AND as a size source (a
        // size from another output says nothing about this one), so with no
        // live sibling nothing is applied at all.
        rig.engine->setRestorePositionPredicate({});
        rig.tracker->belongsToScreen = [](const QRect&, const QString&) {
            return false;
        };
        QVERIFY(rig.tracker->placementStore().record(record(
            rig.engine, QStringLiteral("app|c"), PhosphorEngine::WindowPlacement::stateFloating(), kS1, ownFree)));
        rig.engine->windowOpened(QStringLiteral("app|c2"), kS1, 0, 0);
        QVERIFY(stateOn(rig.engine, kS1)->isFloating(QStringLiteral("app|c2")));
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 0);
    }

    // A recorded rect of a live column's size is a spawn frame the window
    // never chose: the move is refused, and the size arm refuses the same
    // rect as its own-record source, so nothing is applied unless a sibling
    // has a real free rect.
    void ownRecordOfColumnSize_refusedForMoveAndSize()
    {
        Rig rig;
        build(rig);
        m_live.insert(QStringLiteral("col"));
        rig.engine->windowOpened(QStringLiteral("app|col"), kS1, 0, 0);
        const QRect columnRect = rig.engine->lastManagedRect(QStringLiteral("app|col"));
        QVERIFY(columnRect.isValid());

        QSignalSpy geoSpy(rig.engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy sizeSpy(rig.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested);
        QVERIFY(rig.tracker->placementStore().record(record(rig.engine, QStringLiteral("app|old"),
                                                            PhosphorEngine::WindowPlacement::stateFloating(), kS1,
                                                            QRect(QPoint(30, 30), columnRect.size()))));
        rig.engine->windowOpened(QStringLiteral("app|new"), kS1, 0, 0);
        QVERIFY(stateOn(rig.engine, kS1)->isFloating(QStringLiteral("app|new")));
        QCOMPARE(geoSpy.count(), 0);
        QCOMPARE(sizeSpy.count(), 0);
        // The poisoned record was consumed and re-bound, not left for every
        // later reopen to trip over.
        QVERIFY(!rig.tracker->placementStore().contains(QStringLiteral("app|old")));
        QVERIFY(rig.tracker->placementStore().contains(QStringLiteral("app|new")));
    }

    // A floated strip tile keeps its column rect as lastManagedRect: the
    // daemon samples the float-back after the float, and a frame still on
    // the column rect must read as managed, not as a free spot (F580). The
    // unfloat drops it, so the returning tile's first batch is not gated
    // away by a rect equal to the one the strip resolves again.
    void floatKeepsTheColumnRectUntilReadoption()
    {
        Rig rig;
        build(rig);
        const QString w = QStringLiteral("app|w");
        rig.engine->windowOpened(w, kS1, 0, 0);
        const QRect columnRect = rig.engine->lastManagedRect(w);
        QVERIFY(columnRect.isValid());

        rig.engine->toggleWindowFloat(w, kS1);
        QVERIFY(stateOn(rig.engine, kS1)->isFloating(w));
        QCOMPARE(rig.engine->lastManagedRect(w), columnRect);

        QSignalSpy tiled(rig.engine, &ScrollEngine::windowsTiled);
        rig.engine->toggleWindowFloat(w, kS1);
        QVERIFY(stateOn(rig.engine, kS1)->strip().containsWindow(w));
        bool carried = false;
        for (const QList<QVariant>& args : std::as_const(tiled)) {
            const QJsonArray batch = QJsonDocument::fromJson(args.at(0).toString().toUtf8()).array();
            for (const QJsonValue& entry : batch) {
                carried = carried || entry.toObject().value(QLatin1String("windowId")).toString() == w;
            }
        }
        QVERIFY2(carried, "the returning tile must be in a windowsTiled batch");
        QCOMPARE(rig.engine->lastManagedRect(w), columnRect);
    }

    // The size (or move) emit precedes the float sync: the effect's float
    // handler decides whether first-frame suppression has anything left to
    // wait for from what is already in flight.
    void emitOrder_sizeBeforeFloatSync()
    {
        Rig rig;
        build(rig);
        const QRect siblingFree(100, 100, 640, 400);
        QVERIFY(rig.tracker->placementStore().record(record(
            rig.engine, QStringLiteral("app|first"), PhosphorEngine::WindowPlacement::stateTiled(), kS1, siblingFree)));
        m_live.insert(QStringLiteral("first"));
        rig.engine->windowOpened(QStringLiteral("app|first"), kS1, 0, 0);

        QStringList order;
        connect(rig.engine, &PhosphorEngine::PlacementEngineBase::sizeRestoreRequested, this, [&order] {
            order.append(QStringLiteral("size"));
        });
        connect(rig.engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced, this,
                [&order](const QString&, bool floating, const QString&) {
                    order.append(floating ? QStringLiteral("float") : QStringLiteral("unfloat"));
                });
        floatOnly(rig.engine, QStringLiteral("app|second"));
        rig.engine->windowOpened(QStringLiteral("app|second"), kS1, 0, 0);
        QCOMPARE(order, (QStringList{QStringLiteral("size"), QStringLiteral("float")}));
    }

    // A float exit consumes the window's stashed tile, and when that tile was
    // the entry's last one it must hand the stash's blueprint cursor to the
    // state BEFORE retiring the entry. Without the handover the cursor dies
    // with the stash and the next fresh open restarts the template from the
    // live column count.
    void floatExit_consumesStashTileAndCarriesTheBlueprintCursor()
    {
        Rig rig;
        build(rig);
        // One tiled window, stashed, then re-opened floating by rule so the
        // float exit is the path that consumes its tile.
        rig.engine->windowOpened(QStringLiteral("app|only"), kS1, 0, 0);
        ScrollState* before = stateOn(rig.engine, kS1);
        QVERIFY(before);
        QVERIFY(before->strip().containsWindow(QStringLiteral("app|only")));
        before->setBlueprintCursor(3);
        // A distinctive column width, so the stashed tile is identifiable by
        // what a later same-app arrival would inherit from it.
        QVERIFY(before->strip().setActiveColumnWidth(ColumnWidth::makePreset(0.42)));
        const QJsonObject blob = rig.engine->serializeStripState();
        rig.engine->windowClosed(QStringLiteral("app|only"));

        // A fresh engine restores the stash, so the cursor lives only there.
        Rig second;
        build(second);
        second.engine->restoreStripState(blob);
        floatOnly(second.engine, QStringLiteral("app|only"));
        second.engine->windowOpened(QStringLiteral("app|only"), kS1, 0, 0);
        ScrollState* after = stateOn(second.engine, kS1);
        QVERIFY(after);
        QVERIFY(after->isFloating(QStringLiteral("app|only")));
        QCOMPARE(after->blueprintCursor(), 3);

        // And the TILE was claimed, not merely the cursor carried. The entry
        // held one tile, so claiming it retires the whole stash — and a
        // stash that is gone has no shape left for the next same-app arrival
        // to inherit. A tiled open now gets a DEFAULT column, where an
        // unclaimed entry would have handed it the stashed 0.42 preset
        // through the cross-session fuzzy claim.
        //
        // Asserted this way rather than through the save, because a save
        // drops stash tiles naming LIVE windows at write time: the floated
        // window is live, so its id is absent from the blob whether or not
        // the tile was ever claimed.
        second.engine->windowOpened(QStringLiteral("app|next"), kS1, 0, 0);
        ScrollState* tiled = stateOn(second.engine, kS1);
        QVERIFY(tiled);
        QVERIFY(tiled->strip().containsWindow(QStringLiteral("app|next")));
        QCOMPARE(tiled->strip().columns().size(), 1);
        const Column& fresh = tiled->strip().columns().first();
        QVERIFY2(!(fresh.width.kind == ColumnWidth::Preset && qFuzzyCompare(fresh.width.presetFraction, 0.42)),
                 "a claimed stash must leave nothing for the next arrival to inherit");
    }

    // A window that would float on its recorded home screen is not pulled
    // there by the reopen claim: a float is screen-local, and the arrival
    // screen's open floats it where it stands. The claim only ever reaches
    // another virtual screen of the output the window opened on, so the two
    // screens here are virtual screens of one output.
    void crossScreenClaim_declinesAFloatOnTheHomeScreen()
    {
        const QString vs0 = QStringLiteral("M/vs:0");
        const QString vs1 = QStringLiteral("M/vs:1");
        Rig rig;
        rig.settings = new StubScrollSettings(&rig.owner);
        rig.tracker = new StubWindowTracking(&rig.owner);
        rig.tracker->placementStore().setLiveInstanceProbe([this](const QString& windowId) {
            return m_live.contains(PhosphorIdentity::WindowId::extractInstanceId(windowId));
        });
        rig.engine = makeProviderEngine(&rig.owner, {vs0, vs1}, {}, {}, rig.tracker);
        rig.engine->setEngineSettings(rig.settings);
        rig.engine->refreshConfigFromSettings();
        rig.engine->setScrollingModeResolver([](const QString&, int, const QString&) {
            return true;
        });
        QVERIFY(rig.tracker->placementStore().record(record(rig.engine, QStringLiteral("app|old"),
                                                            PhosphorEngine::WindowPlacement::stateTiled(), vs1,
                                                            QRect(10, 10, 500, 400))));

        // Positive control: with nothing floating it, the claim restores it
        // on the other virtual screen of the same output.
        QVERIFY(rig.engine->claimCrossScreenReopen(QStringLiteral("app|home"), vs0, 0, 0));
        QVERIFY(stateOn(rig.engine, vs1)->strip().containsWindow(QStringLiteral("app|home")));
        rig.engine->windowClosed(QStringLiteral("app|home"));

        QVERIFY(rig.tracker->placementStore().record(record(rig.engine, QStringLiteral("app|old2"),
                                                            PhosphorEngine::WindowPlacement::stateTiled(), vs1,
                                                            QRect(10, 10, 500, 400))));
        // Rule-floated on the home screen only.
        rig.engine->setFloatPredicate([vs1](const QString&, const QString& screen) {
            return screen == vs1;
        });
        QVERIFY2(!rig.engine->claimCrossScreenReopen(QStringLiteral("app|new"), vs0, 0, 0),
                 "a float has nothing to restore there");
        QVERIFY(!rig.engine->isWindowTracked(QStringLiteral("app|new")));
        // The record was not consumed by the declined claim.
        QVERIFY(rig.tracker->placementStore().contains(QStringLiteral("app|old2")));

        // The arrival screen's open adopts it. No engine defers an open to
        // a record's home any more, so nothing has to be told the claims
        // declined first.
        rig.engine->windowOpened(QStringLiteral("app|new"), vs0, 0, 0);
        QVERIFY2(stateOn(rig.engine, vs0) && stateOn(rig.engine, vs0)->containsWindow(QStringLiteral("app|new")),
                 "the arrival screen must adopt the window");
    }

    // The reopen contract: a record on ANOTHER output is never claimed, and
    // the open stays on the output it arrived on.
    void crossScreenClaim_neverReachesAnotherOutput()
    {
        Rig rig;
        build(rig);
        rig.engine->setScrollingModeResolver([](const QString&, int, const QString&) {
            return true;
        });
        QVERIFY(rig.tracker->placementStore().record(record(rig.engine, QStringLiteral("app|old"),
                                                            PhosphorEngine::WindowPlacement::stateTiled(), kS2,
                                                            QRect(10, 10, 500, 400))));
        QVERIFY2(!rig.engine->claimCrossScreenReopen(QStringLiteral("app|new"), kS1, 0, 0),
                 "a record on another output must not pull the window there");
        rig.engine->windowOpened(QStringLiteral("app|new"), kS1, 0, 0);
        QVERIFY(stateOn(rig.engine, kS1) && stateOn(rig.engine, kS1)->containsWindow(QStringLiteral("app|new")));
        QVERIFY(!stateOn(rig.engine, kS2) || !stateOn(rig.engine, kS2)->containsWindow(QStringLiteral("app|new")));
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineFreeSize)
#include "test_scrollengine_freesize.moc"
