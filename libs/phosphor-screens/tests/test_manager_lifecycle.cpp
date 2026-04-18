// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Lifecycle tests that exercise ScreenManager's start/stop state machine
// and its wiring to IConfigStore changes. Uses the offscreen QPA so
// QGuiApplication has at least one fake screen to work with.

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/InMemoryConfigStore.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/NoOpPanelSource.h>
#include <PhosphorScreens/VirtualScreen.h>

#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

using Phosphor::Screens::InMemoryConfigStore;
using Phosphor::Screens::NoOpPanelSource;
using Phosphor::Screens::ScreenManager;
using Phosphor::Screens::ScreenManagerConfig;
using Phosphor::Screens::VirtualScreenConfig;
using Phosphor::Screens::VirtualScreenDef;

namespace {

QString physIdForPrimary()
{
    auto* primary = QGuiApplication::primaryScreen();
    return primary ? primary->name() : QString();
}

VirtualScreenDef makeDef(const QString& physId, int index, const QRectF& region)
{
    VirtualScreenDef d;
    d.index = index;
    d.id = PhosphorIdentity::VirtualScreenId::make(physId, index);
    d.physicalScreenId = physId;
    d.displayName = QStringLiteral("VS-%1").arg(index);
    d.region = region;
    return d;
}

} // namespace

class TestManagerLifecycle : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── panelGeometryReady ───

    void testPanelGeometryReadyFiresOnStartWithNoSource()
    {
        ScreenManager mgr(ScreenManagerConfig{/*panelSource=*/nullptr, /*configStore=*/nullptr,
                                              /*useGeometrySensors=*/false});
        QVERIFY(!mgr.isPanelGeometryReady());
        QSignalSpy spy(&mgr, &ScreenManager::panelGeometryReady);
        mgr.start();
        // The ready fan-out is queued to the next event-loop tick.
        QVERIFY(spy.wait(1000));
        QVERIFY(mgr.isPanelGeometryReady());
    }

    void testPanelGeometryReadyFiresOnceEvenAcrossRestart()
    {
        ScreenManager mgr(ScreenManagerConfig{nullptr, nullptr, false});
        QSignalSpy spy(&mgr, &ScreenManager::panelGeometryReady);
        mgr.start();
        QVERIFY(spy.wait(1000));
        const int firstCount = spy.count();
        QCOMPARE(firstCount, 1);

        mgr.stop();
        // isPanelGeometryReady intentionally stays true across restart
        // unless the panel source re-transitions. Second start() should
        // NOT emit again with no source.
        mgr.start();
        QTest::qWait(50);
        QCOMPARE(spy.count(), firstCount);
    }

    void testStopIsIdempotent()
    {
        ScreenManager mgr(ScreenManagerConfig{nullptr, nullptr, false});
        mgr.start();
        mgr.stop();
        mgr.stop(); // second stop must not crash or throw
        QVERIFY(true);
    }

    void testStartIsIdempotent()
    {
        NoOpPanelSource src;
        ScreenManager mgr(ScreenManagerConfig{&src, nullptr, false});
        QSignalSpy spy(&mgr, &ScreenManager::panelGeometryReady);
        mgr.start();
        mgr.start(); // second start must be a no-op
        QTest::qWait(50);
        // NoOpPanelSource has ready()==true from the outset, but
        // ScreenManager's ready transition is driven by panelOffsetsChanged.
        // With no screens changing offsets, it may not emit — but the
        // double-start must not crash or double-wire.
        QVERIFY(spy.count() <= 1);
    }

    // ─── IConfigStore change propagation ───

    void testStoreChangeRefreshesCache()
    {
        const QString physId = physIdForPrimary();
        if (physId.isEmpty()) {
            QSKIP("no primary screen available under offscreen QPA");
        }

        InMemoryConfigStore store;
        ScreenManager mgr(ScreenManagerConfig{nullptr, &store, false});

        mgr.start();
        QVERIFY(!mgr.hasVirtualScreens(physId));

        VirtualScreenConfig cfg;
        cfg.physicalScreenId = physId;
        cfg.screens.append(makeDef(physId, 0, QRectF(0.0, 0.0, 0.5, 1.0)));
        cfg.screens.append(makeDef(physId, 1, QRectF(0.5, 0.0, 0.5, 1.0)));

        QSignalSpy vsSpy(&mgr, &ScreenManager::virtualScreensChanged);
        QVERIFY(store.save(physId, cfg));

        QVERIFY(vsSpy.wait(1000));
        QVERIFY(mgr.hasVirtualScreens(physId));
        QCOMPARE(mgr.virtualScreenIdsFor(physId).size(), 2);
    }

    void testStoreRemovalDropsSubdivision()
    {
        const QString physId = physIdForPrimary();
        if (physId.isEmpty()) {
            QSKIP("no primary screen available under offscreen QPA");
        }

        InMemoryConfigStore store;
        VirtualScreenConfig cfg;
        cfg.physicalScreenId = physId;
        cfg.screens.append(makeDef(physId, 0, QRectF(0.0, 0.0, 0.5, 1.0)));
        cfg.screens.append(makeDef(physId, 1, QRectF(0.5, 0.0, 0.5, 1.0)));
        QVERIFY(store.save(physId, cfg));

        ScreenManager mgr(ScreenManagerConfig{nullptr, &store, false});
        mgr.start();
        QVERIFY(mgr.hasVirtualScreens(physId));

        QVERIFY(store.remove(physId));
        QTest::qWait(50);
        QVERIFY(!mgr.hasVirtualScreens(physId));
    }

    // ─── setVirtualScreenConfig admission ───

    void testSetVirtualScreenConfigRejectsOverCapConfig()
    {
        const QString physId = physIdForPrimary();
        if (physId.isEmpty()) {
            QSKIP("no primary screen available under offscreen QPA");
        }
        ScreenManager mgr(ScreenManagerConfig{nullptr, nullptr, false,
                                              /*maxVirtualScreensPerPhysical=*/2});
        mgr.start();

        VirtualScreenConfig tooMany;
        tooMany.physicalScreenId = physId;
        for (int i = 0; i < 3; ++i) {
            tooMany.screens.append(makeDef(physId, i, QRectF(i / 3.0, 0.0, 1.0 / 3.0, 1.0)));
        }
        QVERIFY(!mgr.setVirtualScreenConfig(physId, tooMany));
    }

    void testRefreshIsIdempotent()
    {
        const QString physId = physIdForPrimary();
        if (physId.isEmpty()) {
            QSKIP("no primary screen available under offscreen QPA");
        }

        InMemoryConfigStore store;
        ScreenManager mgr(ScreenManagerConfig{nullptr, &store, false});
        mgr.start();

        VirtualScreenConfig cfg;
        cfg.physicalScreenId = physId;
        cfg.screens.append(makeDef(physId, 0, QRectF(0.0, 0.0, 0.5, 1.0)));
        cfg.screens.append(makeDef(physId, 1, QRectF(0.5, 0.0, 0.5, 1.0)));
        QVERIFY(store.save(physId, cfg));
        QTest::qWait(50);

        QSignalSpy vsSpy(&mgr, &ScreenManager::virtualScreensChanged);
        // Same payload written again — InMemoryConfigStore's equal-write
        // short-circuit suppresses the changed() signal, so the manager
        // never re-applies.
        QVERIFY(store.save(physId, cfg));
        QTest::qWait(50);
        QCOMPARE(vsSpy.count(), 0);
    }
};

QTEST_MAIN(TestManagerLifecycle)
#include "test_manager_lifecycle.moc"
