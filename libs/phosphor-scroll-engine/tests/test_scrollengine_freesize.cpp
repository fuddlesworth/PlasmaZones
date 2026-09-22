// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The scroll arm of the #1106 free-size restore, beyond the sibling case the
// behaviour suite pins: the lineage gate, the oversized and migration
// refusals, the move-or-size exclusivity of the own-record restore, the
// screen-containment gate on the move, the emit order the effect's float
// handler depends on, and the cross-screen claim declining a float.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"
#include "scrollstubtracking.h"

#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorIdentity/WindowId.h>

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

        // And the TILE was claimed, not merely the cursor carried: the entry
        // held one tile, so consuming it retires the whole stash and the
        // window's id disappears from a fresh save. Without this the carry
        // could be right while the claim block was gone.
        const QJsonObject afterBlob = second.engine->serializeStripState();
        const QByteArray afterText = QJsonDocument(afterBlob).toJson(QJsonDocument::Compact);
        QVERIFY2(!afterText.contains("app|only"), "the claimed stash tile must not survive in the save");
    }

    // A window that would float on its recorded home screen is not pulled
    // home by the cross-screen claim: a float is screen-local, and the
    // arrival screen's open floats it where it stands.
    void crossScreenClaim_declinesAFloatOnTheHomeScreen()
    {
        Rig rig;
        build(rig);
        rig.engine->setScrollingModeResolver([](const QString& screen, int, const QString&) {
            return screen == kS2;
        });
        QVERIFY(rig.tracker->placementStore().record(record(rig.engine, QStringLiteral("app|old"),
                                                            PhosphorEngine::WindowPlacement::stateTiled(), kS2,
                                                            QRect(10, 10, 500, 400))));

        // Positive control: with nothing floating it, the claim pulls it home.
        QVERIFY(rig.engine->claimCrossScreenReopen(QStringLiteral("app|home"), kS1, 0, 0));
        QVERIFY(stateOn(rig.engine, kS2)->strip().containsWindow(QStringLiteral("app|home")));
        rig.engine->windowClosed(QStringLiteral("app|home"));

        QVERIFY(rig.tracker->placementStore().record(record(rig.engine, QStringLiteral("app|old2"),
                                                            PhosphorEngine::WindowPlacement::stateTiled(), kS2,
                                                            QRect(10, 10, 500, 400))));
        // Rule-floated on the home screen only.
        rig.engine->setFloatPredicate([](const QString&, const QString& screen) {
            return screen == kS2;
        });
        QVERIFY2(!rig.engine->claimCrossScreenReopen(QStringLiteral("app|new"), kS1, 0, 0),
                 "a float has nothing to pull home for");
        QVERIFY(!rig.engine->isWindowTracked(QStringLiteral("app|new")));
        // The record was not consumed by the declined claim.
        QVERIFY(rig.tracker->placementStore().contains(QStringLiteral("app|old2")));

        // The dispatch then tells the engine every claim declined, and the
        // arrival screen's open adopts rather than deferring to the record's
        // home a second time.
        rig.engine->noteCrossScreenClaimsExhausted(QStringLiteral("app|new"), true);
        rig.engine->windowOpened(QStringLiteral("app|new"), kS1, 0, 0);
        QVERIFY2(stateOn(rig.engine, kS1) && stateOn(rig.engine, kS1)->containsWindow(QStringLiteral("app|new")),
                 "with the claims exhausted the arrival screen must adopt the window");
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineFreeSize)
#include "test_scrollengine_freesize.moc"
