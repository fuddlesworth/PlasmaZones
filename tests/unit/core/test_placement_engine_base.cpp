// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include <PhosphorEngineApi/PlacementEngineBase.h>
#include <PhosphorEngineApi/IPlacementState.h>

using namespace PhosphorEngineApi;

class ConcreteEngine : public PlacementEngineBase
{
    Q_OBJECT

public:
    explicit ConcreteEngine(QObject* parent = nullptr)
        : PlacementEngineBase(parent)
    {
    }

    int claimedCount = 0;
    int releasedCount = 0;
    int floatedCount = 0;
    int unfloatedCount = 0;

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
    void setWindowFloat(const QString&, bool) override
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

protected:
    void onWindowClaimed(const QString&) override
    {
        ++claimedCount;
    }
    void onWindowReleased(const QString&) override
    {
        ++releasedCount;
    }
    void onWindowFloated(const QString&) override
    {
        ++floatedCount;
    }
    void onWindowUnfloated(const QString&) override
    {
        ++unfloatedCount;
    }
};

class TestPlacementEngineBase : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testInitialState()
    {
        ConcreteEngine engine;
        QCOMPARE(engine.windowState(QStringLiteral("w1")), WindowState::Unmanaged);
        QVERIFY(!engine.hasUnmanagedGeometry(QStringLiteral("w1")));
        QVERIFY(engine.unmanagedGeometries().isEmpty());
    }

    void testClaimWindow_transitionsToEngineOwned()
    {
        ConcreteEngine engine;
        QSignalSpy spy(&engine, &PlacementEngineBase::windowStateTransitioned);
        const QString wid = QStringLiteral("app|uuid1");

        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));

        QCOMPARE(engine.windowState(wid), WindowState::EngineOwned);
        QVERIFY(engine.hasUnmanagedGeometry(wid));
        QCOMPARE(engine.unmanagedGeometry(wid), QRect(10, 20, 300, 200));
        QCOMPARE(engine.unmanagedScreen(wid), QStringLiteral("DP-1"));
        QCOMPARE(engine.claimedCount, 1);
        QCOMPARE(spy.count(), 1);
    }

    void testClaimWindow_emptyIdIgnored()
    {
        ConcreteEngine engine;
        engine.claimWindow(QString(), QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        QCOMPARE(engine.claimedCount, 0);
        QVERIFY(engine.unmanagedGeometries().isEmpty());
    }

    void testClaimWindow_firstOnlySemantics()
    {
        ConcreteEngine engine;
        const QString wid = QStringLiteral("app|uuid1");
        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        engine.claimWindow(wid, QRect(99, 99, 99, 99), QStringLiteral("DP-2"));

        QCOMPARE(engine.unmanagedGeometry(wid), QRect(10, 20, 300, 200));
        QCOMPARE(engine.unmanagedScreen(wid), QStringLiteral("DP-1"));
    }

    void testClaimWindow_overwriteMode()
    {
        ConcreteEngine engine;
        const QString wid = QStringLiteral("app|uuid1");
        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        engine.claimWindow(wid, QRect(99, 99, 99, 99), QStringLiteral("DP-2"), true);

        QCOMPARE(engine.unmanagedGeometry(wid), QRect(99, 99, 99, 99));
        QCOMPARE(engine.unmanagedScreen(wid), QStringLiteral("DP-2"));
    }

    void testReleaseWindow_transitionsToUnmanaged()
    {
        ConcreteEngine engine;
        QSignalSpy restoreSpy(&engine, &PlacementEngineBase::geometryRestoreRequested);
        QSignalSpy stateSpy(&engine, &PlacementEngineBase::windowStateTransitioned);
        const QString wid = QStringLiteral("app|uuid1");

        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        stateSpy.clear();

        engine.releaseWindow(wid);

        QCOMPARE(engine.windowState(wid), WindowState::Unmanaged);
        QVERIFY(!engine.hasUnmanagedGeometry(wid));
        QCOMPARE(engine.releasedCount, 1);
        QCOMPARE(restoreSpy.count(), 1);
        QCOMPARE(stateSpy.count(), 1);
    }

    void testReleaseWindow_unknownIdIgnored()
    {
        ConcreteEngine engine;
        engine.releaseWindow(QStringLiteral("unknown"));
        QCOMPARE(engine.releasedCount, 0);
    }

    void testFloatWindow_transitionsToFloated()
    {
        ConcreteEngine engine;
        const QString wid = QStringLiteral("app|uuid1");
        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));

        engine.floatWindow(wid);

        QCOMPARE(engine.windowState(wid), WindowState::Floated);
        QCOMPARE(engine.floatedCount, 1);
    }

    void testFloatWindow_notEngineOwnedIgnored()
    {
        ConcreteEngine engine;
        engine.floatWindow(QStringLiteral("unknown"));
        QCOMPARE(engine.floatedCount, 0);
    }

    void testUnfloatWindow_transitionsToEngineOwned()
    {
        ConcreteEngine engine;
        const QString wid = QStringLiteral("app|uuid1");
        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        engine.floatWindow(wid);

        engine.unfloatWindow(wid);

        QCOMPARE(engine.windowState(wid), WindowState::EngineOwned);
        QCOMPARE(engine.unfloatedCount, 1);
    }

    void testUnfloatWindow_notFloatedIgnored()
    {
        ConcreteEngine engine;
        const QString wid = QStringLiteral("app|uuid1");
        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));

        engine.unfloatWindow(wid);
        QCOMPARE(engine.unfloatedCount, 0);
    }

    void testClearUnmanagedGeometry_preservesFsmState()
    {
        ConcreteEngine engine;
        const QString wid = QStringLiteral("app|uuid1");
        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));

        engine.clearUnmanagedGeometry(wid);

        QVERIFY(!engine.hasUnmanagedGeometry(wid));
        QCOMPARE(engine.windowState(wid), WindowState::EngineOwned);
    }

    void testForgetWindow_clearsEverything()
    {
        ConcreteEngine engine;
        const QString wid = QStringLiteral("app|uuid1");
        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));

        engine.forgetWindow(wid);

        QVERIFY(!engine.hasUnmanagedGeometry(wid));
        QCOMPARE(engine.windowState(wid), WindowState::Unmanaged);
    }

    void testStoreUnmanagedGeometry_invalidGeometryIgnored()
    {
        ConcreteEngine engine;
        engine.storeUnmanagedGeometry(QStringLiteral("w1"), QRect(), QStringLiteral("DP-1"));
        QVERIFY(!engine.hasUnmanagedGeometry(QStringLiteral("w1")));
    }

    void testEviction_capsAt200()
    {
        ConcreteEngine engine;
        for (int i = 0; i < 210; ++i) {
            engine.storeUnmanagedGeometry(QStringLiteral("w%1").arg(i), QRect(0, 0, 100, 100), QStringLiteral("DP-1"),
                                          true);
        }
        QVERIFY(engine.unmanagedGeometries().size() <= 200);
        QVERIFY(engine.hasUnmanagedGeometry(QStringLiteral("w209")));
    }

    void testSetUnmanagedGeometries_populatesFsm()
    {
        ConcreteEngine engine;
        QHash<QString, PlacementEngineBase::UnmanagedEntry> geos;
        geos[QStringLiteral("w1")] = {QRect(0, 0, 100, 100), QStringLiteral("DP-1")};
        geos[QStringLiteral("w2")] = {QRect(0, 0, 200, 200), QStringLiteral("DP-2")};

        engine.setUnmanagedGeometries(geos);

        QCOMPARE(engine.windowState(QStringLiteral("w1")), WindowState::EngineOwned);
        QCOMPARE(engine.windowState(QStringLiteral("w2")), WindowState::EngineOwned);
        QCOMPARE(engine.unmanagedGeometries().size(), 2);
    }

    void testPruneStaleWindows_removesDeadEntries()
    {
        ConcreteEngine engine;
        engine.claimWindow(QStringLiteral("alive"), QRect(0, 0, 100, 100), QStringLiteral("DP-1"));
        engine.claimWindow(QStringLiteral("dead1"), QRect(0, 0, 100, 100), QStringLiteral("DP-1"));
        engine.claimWindow(QStringLiteral("dead2"), QRect(0, 0, 100, 100), QStringLiteral("DP-1"));

        QSet<QString> alive{QStringLiteral("alive")};
        int pruned = engine.pruneStaleWindows(alive);

        QCOMPARE(pruned, 2);
        QVERIFY(engine.hasUnmanagedGeometry(QStringLiteral("alive")));
        QVERIFY(!engine.hasUnmanagedGeometry(QStringLiteral("dead1")));
        QCOMPARE(engine.windowState(QStringLiteral("dead1")), WindowState::Unmanaged);
    }

    void testSerializeDeserialize_roundTrip()
    {
        ConcreteEngine engine;
        engine.claimWindow(QStringLiteral("w1"), QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        engine.claimWindow(QStringLiteral("w2"), QRect(50, 60, 400, 300), QStringLiteral("DP-2"));

        QJsonObject serialized = engine.serializeBaseState();

        ConcreteEngine engine2;
        engine2.deserializeBaseState(serialized);

        QCOMPARE(engine2.unmanagedGeometry(QStringLiteral("w1")), QRect(10, 20, 300, 200));
        QCOMPARE(engine2.unmanagedScreen(QStringLiteral("w1")), QStringLiteral("DP-1"));
        QCOMPARE(engine2.unmanagedGeometry(QStringLiteral("w2")), QRect(50, 60, 400, 300));
        QCOMPARE(engine2.windowState(QStringLiteral("w1")), WindowState::EngineOwned);
        QCOMPARE(engine2.windowState(QStringLiteral("w2")), WindowState::EngineOwned);
    }

    void testDeserialize_skipsInvalidEntries()
    {
        QJsonObject state;
        QJsonObject geos;

        QJsonObject valid;
        valid[QLatin1String("x")] = 10;
        valid[QLatin1String("y")] = 20;
        valid[QLatin1String("w")] = 300;
        valid[QLatin1String("h")] = 200;
        geos[QStringLiteral("w1")] = valid;

        QJsonObject zeroSize;
        zeroSize[QLatin1String("x")] = 10;
        zeroSize[QLatin1String("y")] = 20;
        zeroSize[QLatin1String("w")] = 0;
        zeroSize[QLatin1String("h")] = 0;
        geos[QStringLiteral("w2")] = zeroSize;

        geos[QString()] = valid;

        state[QLatin1String("unmanagedGeometries")] = geos;

        ConcreteEngine engine;
        engine.deserializeBaseState(state);

        QCOMPARE(engine.unmanagedGeometries().size(), 1);
        QVERIFY(engine.hasUnmanagedGeometry(QStringLiteral("w1")));
    }

    void testFsmTransitionSequence_fullLifecycle()
    {
        ConcreteEngine engine;
        const QString wid = QStringLiteral("app|uuid1");

        QCOMPARE(engine.windowState(wid), WindowState::Unmanaged);

        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        QCOMPARE(engine.windowState(wid), WindowState::EngineOwned);

        engine.floatWindow(wid);
        QCOMPARE(engine.windowState(wid), WindowState::Floated);

        engine.unfloatWindow(wid);
        QCOMPARE(engine.windowState(wid), WindowState::EngineOwned);

        engine.releaseWindow(wid);
        QCOMPARE(engine.windowState(wid), WindowState::Unmanaged);
    }

    void testFloatWindow_doubleFloatIsIdempotent()
    {
        ConcreteEngine engine;
        const QString wid = QStringLiteral("app|uuid1");
        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        engine.floatWindow(wid);

        engine.floatWindow(wid);

        QCOMPARE(engine.windowState(wid), WindowState::Floated);
        QCOMPARE(engine.floatedCount, 1);
    }

    void testEviction_preservesCurrentWindowId()
    {
        ConcreteEngine engine;
        for (int i = 0; i < 200; ++i) {
            engine.storeUnmanagedGeometry(QStringLiteral("old%1").arg(i), QRect(0, 0, 100, 100), QStringLiteral("DP-1"),
                                          true);
        }

        const QString current = QStringLiteral("current");
        engine.storeUnmanagedGeometry(current, QRect(0, 0, 100, 100), QStringLiteral("DP-1"), true);

        QVERIFY(engine.hasUnmanagedGeometry(current));
        QVERIFY(engine.unmanagedGeometries().size() <= 200);
    }

    void testDeserialize_clearsPriorState()
    {
        ConcreteEngine engine;
        engine.claimWindow(QStringLiteral("prior"), QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        engine.floatWindow(QStringLiteral("prior"));
        QCOMPARE(engine.windowState(QStringLiteral("prior")), WindowState::Floated);

        QJsonObject state;
        QJsonObject geos;
        QJsonObject entry;
        entry[QLatin1String("x")] = 50;
        entry[QLatin1String("y")] = 60;
        entry[QLatin1String("w")] = 400;
        entry[QLatin1String("h")] = 300;
        geos[QStringLiteral("fresh")] = entry;
        state[QLatin1String("unmanagedGeometries")] = geos;

        engine.deserializeBaseState(state);

        QCOMPARE(engine.windowState(QStringLiteral("prior")), WindowState::Unmanaged);
        QVERIFY(!engine.hasUnmanagedGeometry(QStringLiteral("prior")));
        QCOMPARE(engine.windowState(QStringLiteral("fresh")), WindowState::EngineOwned);
        QVERIFY(engine.hasUnmanagedGeometry(QStringLiteral("fresh")));
    }

    void testReleaseFromFloated_emitsRestoreSignal()
    {
        ConcreteEngine engine;
        QSignalSpy restoreSpy(&engine, &PlacementEngineBase::geometryRestoreRequested);
        const QString wid = QStringLiteral("app|uuid1");

        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        engine.floatWindow(wid);

        engine.releaseWindow(wid);

        QCOMPARE(engine.windowState(wid), WindowState::Unmanaged);
        QCOMPARE(engine.releasedCount, 1);
        QCOMPARE(restoreSpy.count(), 1);
    }

    void testHookSeesCurrentState()
    {
        struct StateCheckEngine : public PlacementEngineBase
        {
            WindowState stateInClaim = WindowState::Unmanaged;
            WindowState stateInRelease = WindowState::Unmanaged;
            WindowState stateInFloat = WindowState::Unmanaged;
            WindowState stateInUnfloat = WindowState::Unmanaged;
            StateCheckEngine()
                : PlacementEngineBase(nullptr)
            {
            }
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
            void setWindowFloat(const QString&, bool) override
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

        protected:
            void onWindowClaimed(const QString& wid) override
            {
                stateInClaim = windowState(wid);
            }
            void onWindowReleased(const QString& wid) override
            {
                stateInRelease = windowState(wid);
            }
            void onWindowFloated(const QString& wid) override
            {
                stateInFloat = windowState(wid);
            }
            void onWindowUnfloated(const QString& wid) override
            {
                stateInUnfloat = windowState(wid);
            }
        };

        StateCheckEngine engine;
        const QString wid = QStringLiteral("app|uuid1");

        engine.claimWindow(wid, QRect(10, 20, 300, 200), QStringLiteral("DP-1"));
        QCOMPARE(engine.stateInClaim, WindowState::EngineOwned);

        engine.floatWindow(wid);
        QCOMPARE(engine.stateInFloat, WindowState::Floated);

        engine.unfloatWindow(wid);
        QCOMPARE(engine.stateInUnfloat, WindowState::EngineOwned);

        engine.releaseWindow(wid);
        QCOMPARE(engine.stateInRelease, WindowState::Unmanaged);
    }
};

QTEST_MAIN(TestPlacementEngineBase)
#include "test_placement_engine_base.moc"
