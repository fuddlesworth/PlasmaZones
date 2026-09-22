// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/IPlacementState.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include "helpers/WindowPlacementBuilders.h"
// The scroll engine's tracker stub: a real WindowPlacementStore behind the
// IWindowTrackingService surface, with a screen-membership knob and no screen
// manager, which is exactly what the shared free-size restore consults.
#include "scrollstubtracking.h"

using namespace PhosphorEngine;

class ConcreteEngine : public PlacementEngineBase
{
    Q_OBJECT

public:
    explicit ConcreteEngine(QObject* parent = nullptr)
        : PlacementEngineBase(parent)
    {
    }
    using PlacementEngineBase::placedByPreviousLineage;
    using PlacementEngineBase::restoreFreeSizeWhereItStands;

    bool isActiveOnScreen(const QString&) const override
    {
        return true;
    }
    void windowOpened(const QString&, const QString&, int, int) override
    {
    }
    void windowClosed(const QString&) override
    {
    }
    void windowFocused(const QString&, const QString&) override
    {
    }
    void toggleWindowFloat(const QString&, const QString&) override
    {
    }
    void setWindowFloat(const QString&, bool, const QString&) override
    {
    }
    void focusInDirection(const QString&, const NavigationContext&) override
    {
    }
    void moveFocusedInDirection(const QString&, const NavigationContext&) override
    {
    }
    void swapFocusedInDirection(const QString&, const NavigationContext&) override
    {
    }
    void moveFocusedToPosition(int, const NavigationContext&) override
    {
    }
    void rotateWindows(bool, const NavigationContext&) override
    {
    }
    void reapplyLayout(const NavigationContext&) override
    {
    }
    void snapAllWindows(const NavigationContext&) override
    {
    }
    void cycleFocus(bool, const NavigationContext&) override
    {
    }
    void pushToEmptyZone(const NavigationContext&) override
    {
    }
    void restoreFocusedWindow(const NavigationContext&) override
    {
    }
    void toggleFocusedFloat(const NavigationContext&) override
    {
    }
    void saveState() override
    {
    }
    void loadState() override
    {
    }
    IPlacementState* stateForScreen(const QString&) override
    {
        return nullptr;
    }
    const IPlacementState* stateForScreen(const QString&) const override
    {
        return nullptr;
    }
};

// The per-engine unmanaged-geometry store and WindowState FSM were removed from
// PlacementEngineBase: float-back / free geometry now lives solely in the unified
// WindowPlacementStore (one record per window, shared freeGeometryByScreen). The
// base class is a thin shell — settings injection and a no-op base prune — so this
// test covers only that remaining surface. Float-back behavior is covered by the
// WindowPlacementStore + WindowTrackingService + WTA tests.
class TestPlacementEngineBase : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testEngineSettings_setAndGet()
    {
        ConcreteEngine engine;
        QCOMPARE(engine.engineSettings(), nullptr);

        QObject settings;
        engine.setEngineSettings(&settings);
        QCOMPARE(engine.engineSettings(), &settings);
    }

    void testEngineSettings_nullptrIgnored()
    {
        ConcreteEngine engine;
        QObject settings;
        engine.setEngineSettings(&settings);

        engine.setEngineSettings(nullptr); // rejected — keeps the prior value
        QCOMPARE(engine.engineSettings(), &settings);
    }

    void testPruneStaleWindows_baseKeepsNoState()
    {
        // The base no longer holds per-window state, so its prune is a no-op (0).
        // Engines override and add their own pruning.
        ConcreteEngine engine;
        QCOMPARE(engine.pruneStaleWindows({QStringLiteral("alive")}), 0);
    }

    // ── restoreFreeSizeWhereItStands (#1106), the shared contract ──────────

    static PhosphorEngine::WindowPlacement floatingRecord(const QString& windowId, const QRect& rect)
    {
        return PlasmaZones::TestHelpers::makePlacement(windowId, QStringLiteral("app"),
                                                       WindowPlacement::stateFloating(),
                                                       WindowPlacement::snapEngineId(), QStringLiteral("S1"), rect);
    }

    // The managed-size refusal is a two-pixel tolerance on BOTH axes: exactly
    // two is refused, three on either axis is accepted.
    void testFreeSize_managedSizeToleranceBoundary()
    {
        ScrollTestUtils::StubWindowTracking tracker;
        QSet<QString> live{QStringLiteral("sib")};
        tracker.placementStore().setLiveInstanceProbe(PlasmaZones::TestHelpers::liveInstanceProbe(live));
        ConcreteEngine engine;
        QSignalSpy sizeSpy(&engine, &PlacementEngineBase::sizeRestoreRequested);
        const QList<QSize> managed{QSize(800, 600)};
        const QString s1 = QStringLiteral("S1");

        const auto restoreWith = [&](const QRect& siblingRect) {
            QVERIFY(tracker.placementStore().record(floatingRecord(QStringLiteral("app|sib"), siblingRect)));
            engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open,
                                                /*placedBefore=*/false, managed);
        };
        restoreWith(QRect(0, 0, 802, 602));
        QCOMPARE(sizeSpy.count(), 0);
        restoreWith(QRect(0, 0, 798, 598));
        QCOMPARE(sizeSpy.count(), 0);
        restoreWith(QRect(0, 0, 803, 600));
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(803, 600));
        restoreWith(QRect(0, 0, 800, 603));
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(800, 603));
    }

    // Only Unminimize and a window a previous lineage placed are refused;
    // every other driver can be a first placement.
    void testFreeSize_reasonAndLineageGates()
    {
        ScrollTestUtils::StubWindowTracking tracker;
        QSet<QString> live{QStringLiteral("sib")};
        tracker.placementStore().setLiveInstanceProbe(PlasmaZones::TestHelpers::liveInstanceProbe(live));
        QVERIFY(tracker.placementStore().record(floatingRecord(QStringLiteral("app|sib"), QRect(0, 0, 640, 480))));
        ConcreteEngine engine;
        QSignalSpy sizeSpy(&engine, &PlacementEngineBase::sizeRestoreRequested);
        const QString s1 = QStringLiteral("S1");
        for (const RestoreReason reason : {RestoreReason::Open, RestoreReason::PendingSweep,
                                           RestoreReason::DesktopArrival, RestoreReason::DaemonRestartSweep}) {
            engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, reason, false, {});
        }
        QCOMPARE(sizeSpy.count(), 4);
        sizeSpy.clear();
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Unminimize, false,
                                            {});
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open,
                                            /*placedBefore=*/true, {});
        QCOMPARE(sizeSpy.count(), 0);
        // Guards: no tracker, empty ids.
        engine.restoreFreeSizeWhereItStands(nullptr, QStringLiteral("app|new"), s1, RestoreReason::Open, false, {});
        engine.restoreFreeSizeWhereItStands(&tracker, QString(), s1, RestoreReason::Open, false, {});
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), QString(), RestoreReason::Open, false,
                                            {});
        QCOMPARE(sizeSpy.count(), 0);
    }

    // Source order: own record with an engine slot, else the earliest live
    // sibling, else a closed same-app record. A slot-less own record (the
    // pre-tile stub) is never a source, and an own rect that is unusable
    // falls through to the sibling rather than ending the search.
    void testFreeSize_sourceOrderAndStub()
    {
        ScrollTestUtils::StubWindowTracking tracker;
        QSet<QString> live{QStringLiteral("sib")};
        tracker.placementStore().setLiveInstanceProbe(PlasmaZones::TestHelpers::liveInstanceProbe(live));
        ConcreteEngine engine;
        QSignalSpy sizeSpy(&engine, &PlacementEngineBase::sizeRestoreRequested);
        const QString s1 = QStringLiteral("S1");
        auto& store = tracker.placementStore();

        // A closed record alone is a source (tier 3).
        QVERIFY(store.record(floatingRecord(QStringLiteral("app|closed"), QRect(0, 0, 500, 400))));
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open, false, {});
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(500, 400));
        QCOMPARE(store.size(), 1); // read, never consumed

        // A live sibling beats the closed record (tier 2).
        QVERIFY(store.record(floatingRecord(QStringLiteral("app|sib"), QRect(0, 0, 640, 480))));
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open, false, {});
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(640, 480));

        // The window's own slot-less stub (its spawn frame) is ignored even
        // when its size is not a managed one.
        WindowPlacement stub;
        stub.windowId = QStringLiteral("app|new");
        stub.appId = QStringLiteral("app");
        stub.screenId = s1;
        stub.freeGeometryByScreen.insert(s1, QRect(0, 0, 999, 777));
        QVERIFY(store.record(stub));
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open, false, {});
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(640, 480));

        // An own record WITH a slot wins (tier 1)...
        QVERIFY(store.record(floatingRecord(QStringLiteral("app|new"), QRect(0, 0, 900, 650))));
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open, false, {});
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(900, 650));
        // ...unless its rect is unusable (a managed size), when the sibling
        // is consulted instead.
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open, false,
                                            {QSize(900, 650)});
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(640, 480));
    }

    // The clamp uses the tracker's available area only when it is non-empty:
    // an unresolvable screen's invalid rect has a 0x0 size that must not
    // become the bound.
    void testFreeSize_clampNeedsANonEmptyAvailableArea()
    {
        ScrollTestUtils::StubWindowTracking tracker;
        QSet<QString> live{QStringLiteral("sib")};
        tracker.placementStore().setLiveInstanceProbe(PlasmaZones::TestHelpers::liveInstanceProbe(live));
        QVERIFY(tracker.placementStore().record(floatingRecord(QStringLiteral("app|sib"), QRect(0, 0, 2500, 1500))));
        ConcreteEngine engine;
        QSignalSpy sizeSpy(&engine, &PlacementEngineBase::sizeRestoreRequested);
        const QString s1 = QStringLiteral("S1");
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open, false, {});
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(2500, 1500));
        tracker.availableGeometry = QRect(0, 0, 1920, 1080);
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open, false, {});
        QCOMPARE(sizeSpy.takeFirst().at(1).toSize(), QSize(1920, 1080));
        // Screen-local: a rect the tracker places elsewhere is not a source.
        tracker.belongsToScreen = [](const QRect&, const QString&) {
            return false;
        };
        engine.restoreFreeSizeWhereItStands(&tracker, QStringLiteral("app|new"), s1, RestoreReason::Open, false, {});
        QCOMPARE(sizeSpy.count(), 0);
    }

    void testPlacedByPreviousLineage_requiresAnEngineSlot()
    {
        WindowPlacementStore store;
        QVERIFY(!ConcreteEngine::placedByPreviousLineage(store, QStringLiteral("app|w")));
        WindowPlacement stub;
        stub.windowId = QStringLiteral("app|w");
        stub.appId = QStringLiteral("app");
        stub.screenId = QStringLiteral("S1");
        stub.freeGeometryByScreen.insert(QStringLiteral("S1"), QRect(0, 0, 10, 10));
        QVERIFY(store.record(stub));
        QVERIFY(!ConcreteEngine::placedByPreviousLineage(store, QStringLiteral("app|w")));
        QVERIFY(store.record(floatingRecord(QStringLiteral("app|w"), QRect(0, 0, 10, 10))));
        QVERIFY(ConcreteEngine::placedByPreviousLineage(store, QStringLiteral("app|w")));
        QVERIFY(!ConcreteEngine::placedByPreviousLineage(store, QString()));
    }
};

QTEST_MAIN(TestPlacementEngineBase)
#include "test_placement_engine_base.moc"
