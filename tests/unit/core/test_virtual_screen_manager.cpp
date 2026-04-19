// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_virtual_screen_manager.cpp
 * @brief Unit tests for Phosphor::Screens::ScreenManager virtual screen management methods
 *
 * Tests config storage (set/get), virtualScreenIdsFor(), hasVirtualScreens(),
 * effectiveScreenIds(), and the virtualScreensChanged signal.
 *
 * Note: Methods that depend on physical QScreen objects (screenGeometry,
 * virtualScreenAt, effectiveScreenAt) are not easily testable without a
 * display server, so we test the config-management layer that does not
 * require real QScreens.
 */

#include <QTest>
#include <QSignalSpy>

#include "core/screenmanagerservice.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "helpers/VirtualScreenTestHelpers.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

class TestVirtualScreenManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════════
    // setVirtualScreenConfig / virtualScreenConfig round-trip
    // ═══════════════════════════════════════════════════════════════════════════

    void testSetAndGetConfig()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        auto config = makeSplitConfig(physId);

        mgr.setVirtualScreenConfig(physId, config);
        auto retrieved = mgr.virtualScreenConfig(physId);

        QCOMPARE(retrieved, config);
    }

    void testGetConfig_noConfig_returnsEmpty()
    {
        Phosphor::Screens::ScreenManager mgr;
        auto config = mgr.virtualScreenConfig(QStringLiteral("nonexistent"));

        QVERIFY(config.isEmpty());
        QVERIFY(!config.hasSubdivisions());
    }

    void testSetEmptyConfig_clearsSubdivisions()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        // Set a real config, then clear it
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));
        QVERIFY(mgr.hasVirtualScreens(physId));

        mgr.setVirtualScreenConfig(physId, Phosphor::Screens::VirtualScreenConfig{});
        QVERIFY(!mgr.hasVirtualScreens(physId));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // hasVirtualScreens
    // ═══════════════════════════════════════════════════════════════════════════

    void testHasVirtualScreens_noConfig_returnsFalse()
    {
        Phosphor::Screens::ScreenManager mgr;
        QVERIFY(!mgr.hasVirtualScreens(QStringLiteral("Dell:U2722D:115107")));
    }

    void testHasVirtualScreens_withTwoScreens_returnsTrue()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QVERIFY(mgr.hasVirtualScreens(physId));
    }

    void testHasVirtualScreens_singleScreen_returnsFalse()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        Phosphor::Screens::VirtualScreenConfig config;
        config.physicalScreenId = physId;
        config.screens.append(Phosphor::Screens::VirtualScreenDef{
            PhosphorIdentity::VirtualScreenId::make(physId, 0), physId, QStringLiteral("Full"), QRectF(0, 0, 1, 1), 0});

        mgr.setVirtualScreenConfig(physId, config);
        // Single screen = no subdivision
        QVERIFY(!mgr.hasVirtualScreens(physId));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // virtualScreenIdsFor
    // ═══════════════════════════════════════════════════════════════════════════

    void testVirtualScreenIdsFor_noConfig_returnsPhysicalId()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        QStringList ids = mgr.virtualScreenIdsFor(physId);
        QCOMPARE(ids.size(), 1);
        QCOMPARE(ids.first(), physId);
    }

    void testVirtualScreenIdsFor_twoVirtualScreens()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QStringList ids = mgr.virtualScreenIdsFor(physId);
        QCOMPARE(ids.size(), 2);
        QCOMPARE(ids.at(0), PhosphorIdentity::VirtualScreenId::make(physId, 0));
        QCOMPARE(ids.at(1), PhosphorIdentity::VirtualScreenId::make(physId, 1));
    }

    void testVirtualScreenIdsFor_threeVirtualScreens()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("LG:27GP850:XYZ789");
        mgr.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));

        QStringList ids = mgr.virtualScreenIdsFor(physId);
        QCOMPARE(ids.size(), 3);
        QCOMPARE(ids.at(0), PhosphorIdentity::VirtualScreenId::make(physId, 0));
        QCOMPARE(ids.at(1), PhosphorIdentity::VirtualScreenId::make(physId, 1));
        QCOMPARE(ids.at(2), PhosphorIdentity::VirtualScreenId::make(physId, 2));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // effectiveScreenIds (no QScreens tracked -> depends on virtual configs)
    // ═══════════════════════════════════════════════════════════════════════════

    void testEffectiveScreenIds_noScreensOrConfigs_returnsEmpty()
    {
        Phosphor::Screens::ScreenManager mgr;
        // No QScreens tracked and no virtual configs -> empty
        QStringList ids = mgr.effectiveScreenIds();
        QVERIFY(ids.isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // virtualScreensChanged signal
    // ═══════════════════════════════════════════════════════════════════════════

    void testSignal_virtualScreensChanged_emittedOnConfigSet()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), physId);
    }

    void testSignal_virtualScreensChanged_emittedOnConfigClear()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
        mgr.setVirtualScreenConfig(physId, Phosphor::Screens::VirtualScreenConfig{});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), physId);
    }

    /// A pure region edit (same ids, same display names, different rects)
    /// is the regions-only path: virtualScreenRegionsChanged fires and
    /// virtualScreensChanged does NOT — so handlers attached to topology
    /// changes don't run for what is just a geometry update.
    void testSignal_regionEdit_firesRegionsChangedOnly()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QSignalSpy topologySpy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
        QSignalSpy regionsSpy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreenRegionsChanged);

        // Same ids and display names, just a different split point.
        Phosphor::Screens::VirtualScreenConfig edited;
        edited.physicalScreenId = physId;
        edited.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0.0, 0.0, 0.7, 1.0)));
        edited.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.7, 0.0, 0.3, 1.0)));
        QVERIFY(mgr.setVirtualScreenConfig(physId, edited));

        QCOMPARE(regionsSpy.count(), 1);
        QCOMPARE(regionsSpy.first().first().toString(), physId);
        QCOMPARE(topologySpy.count(), 0);
    }

    /// A pure rename (same ids, same regions, different displayName) is
    /// topology-adjacent — the OSD label changes and downstream listeners
    /// that hash on display name need to be told. It must fire the full
    /// virtualScreensChanged signal, not the lightweight regions-only one.
    void testSignal_displayNameOnly_firesVirtualScreensChanged()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QSignalSpy topologySpy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
        QSignalSpy regionsSpy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreenRegionsChanged);

        // Same ids, same regions, just renamed display names.
        Phosphor::Screens::VirtualScreenConfig renamed;
        renamed.physicalScreenId = physId;
        renamed.screens.append(makeDef(physId, 0, QStringLiteral("Primary"), QRectF(0.0, 0.0, 0.5, 1.0)));
        renamed.screens.append(makeDef(physId, 1, QStringLiteral("Secondary"), QRectF(0.5, 0.0, 0.5, 1.0)));
        QVERIFY(mgr.setVirtualScreenConfig(physId, renamed));

        QCOMPARE(topologySpy.count(), 1);
        QCOMPARE(topologySpy.first().first().toString(), physId);
        QCOMPARE(regionsSpy.count(), 0);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Config replacement
    // ═══════════════════════════════════════════════════════════════════════════

    void testConfigReplacement_overwritesPrevious()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));
        QCOMPARE(mgr.virtualScreenIdsFor(physId).size(), 2);

        mgr.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));
        QCOMPARE(mgr.virtualScreenIdsFor(physId).size(), 3);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Multiple physical screens with independent configs
    // ═══════════════════════════════════════════════════════════════════════════

    void testMultiplePhysicalScreens_independentConfigs()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physA = QStringLiteral("Dell:U2722D:111");
        const QString physB = QStringLiteral("LG:27GP850:222");

        mgr.setVirtualScreenConfig(physA, makeSplitConfig(physA));
        mgr.setVirtualScreenConfig(physB, makeThreeWayConfig(physB));

        QCOMPARE(mgr.virtualScreenIdsFor(physA).size(), 2);
        QCOMPARE(mgr.virtualScreenIdsFor(physB).size(), 3);
        QVERIFY(mgr.hasVirtualScreens(physA));
        QVERIFY(mgr.hasVirtualScreens(physB));

        // Clear one, the other remains
        mgr.setVirtualScreenConfig(physA, Phosphor::Screens::VirtualScreenConfig{});
        QVERIFY(!mgr.hasVirtualScreens(physA));
        QVERIFY(mgr.hasVirtualScreens(physB));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Overlapping region rejection
    // ═══════════════════════════════════════════════════════════════════════════

    void testSetVirtualScreenConfig_overlapping_rejected()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        // Build a config with two overlapping regions: both start at x=0 and
        // share a 0.1-wide overlap band, well above Phosphor::Screens::VirtualScreenDef::Tolerance.
        Phosphor::Screens::VirtualScreenConfig config;
        config.physicalScreenId = physId;
        // Left: [0.0, 0.6) — Right: [0.5, 1.0) — overlap = [0.5, 0.6), width=0.1
        config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0.0, 0.0, 0.6, 1.0)));
        config.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.5, 0.0, 0.5, 1.0)));

        bool result = mgr.setVirtualScreenConfig(physId, config);

        QVERIFY2(!result, "setVirtualScreenConfig must reject overlapping regions");
        QVERIFY2(!mgr.hasVirtualScreens(physId),
                 "hasVirtualScreens must return false after overlapping config is rejected");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // refreshVirtualConfigs — Settings → Phosphor::Screens::ScreenManager observer pattern
    // ═══════════════════════════════════════════════════════════════════════════

    /// Refreshing with a brand-new map applies all entries and emits one
    /// virtualScreensChanged signal per added entry.
    void testRefreshVirtualConfigs_addsNewEntries()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physA = QStringLiteral("Dell:U2722D:111");
        const QString physB = QStringLiteral("LG:27GP850:222");

        QHash<QString, Phosphor::Screens::VirtualScreenConfig> configs;
        configs.insert(physA, makeSplitConfig(physA));
        configs.insert(physB, makeThreeWayConfig(physB));

        QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
        mgr.refreshVirtualConfigs(configs);

        QVERIFY(mgr.hasVirtualScreens(physA));
        QVERIFY(mgr.hasVirtualScreens(physB));
        QCOMPARE(mgr.virtualScreenIdsFor(physA).size(), 2);
        QCOMPARE(mgr.virtualScreenIdsFor(physB).size(), 3);
        QCOMPARE(spy.count(), 2);
    }

    /// Refreshing with an empty map tears down existing subdivisions.
    void testRefreshVirtualConfigs_removesMissingEntries()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        mgr.setVirtualScreenConfig(physId, makeSplitConfig(physId));
        QVERIFY(mgr.hasVirtualScreens(physId));

        QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
        mgr.refreshVirtualConfigs({});

        QVERIFY(!mgr.hasVirtualScreens(physId));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), physId);
    }

    /// Refreshing with the same map is a no-op (no signals fired).
    void testRefreshVirtualConfigs_idempotentForUnchanged()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const auto config = makeSplitConfig(physId);
        mgr.setVirtualScreenConfig(physId, config);

        QHash<QString, Phosphor::Screens::VirtualScreenConfig> configs;
        configs.insert(physId, config);

        QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
        mgr.refreshVirtualConfigs(configs);

        QCOMPARE(spy.count(), 0);
        QVERIFY(mgr.hasVirtualScreens(physId));
    }

    /// Mixed delta: one entry added, one entry removed, one entry unchanged.
    /// Exactly two virtualScreensChanged signals fire (added + removed).
    void testRefreshVirtualConfigs_mixedDelta()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physA = QStringLiteral("Dell:U2722D:111");
        const QString physB = QStringLiteral("LG:27GP850:222");
        const QString physC = QStringLiteral("Asus:VG279QM:333");

        // Initial state: A and B configured
        mgr.setVirtualScreenConfig(physA, makeSplitConfig(physA));
        mgr.setVirtualScreenConfig(physB, makeSplitConfig(physB));

        // New state: A unchanged, B removed, C added
        QHash<QString, Phosphor::Screens::VirtualScreenConfig> configs;
        configs.insert(physA, makeSplitConfig(physA));
        configs.insert(physC, makeSplitConfig(physC));

        QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
        mgr.refreshVirtualConfigs(configs);

        QVERIFY(mgr.hasVirtualScreens(physA));
        QVERIFY(!mgr.hasVirtualScreens(physB));
        QVERIFY(mgr.hasVirtualScreens(physC));
        QCOMPARE(spy.count(), 2);

        // Verify both physB (removed) and physC (added) fired exactly once;
        // physA (unchanged) did not fire.
        QSet<QString> firedFor;
        for (int i = 0; i < spy.count(); ++i) {
            firedFor.insert(spy.at(i).first().toString());
        }
        QVERIFY(firedFor.contains(physB));
        QVERIFY(firedFor.contains(physC));
        QVERIFY(!firedFor.contains(physA));
    }

    /// Regression test for the daemon's initial-sync ordering at boot:
    /// `Daemon::connectScreenSignals` calls `refreshVirtualConfigs` BEFORE
    /// connecting `onVirtualScreensReconfigured` to `virtualScreensChanged`.
    /// The intent is that the boot-time refresh populates the cache silently
    /// — without triggering the migration / autotile / resnap fan-out, which
    /// is unnecessary at startup (no windows yet) and risks running against
    /// half-initialized state. Qt signals do NOT replay past emissions, so
    /// connecting AFTER the refresh must yield zero historical signals.
    /// If anyone reorders the connect-then-refresh pattern in the daemon,
    /// the refresh will trigger fan-out at boot and this invariant will be
    /// silently broken — this test exists to lock the contract in.
    void testRefreshVirtualConfigs_emissionsBeforeConnectAreNotReplayed()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");

        // Refresh first (no listener attached). This is the boot-time
        // initial sync — the cache gets populated, virtualScreensChanged
        // fires, but nobody is listening yet.
        QHash<QString, Phosphor::Screens::VirtualScreenConfig> configs{{physId, makeSplitConfig(physId)}};
        mgr.refreshVirtualConfigs(configs);
        QVERIFY(mgr.hasVirtualScreens(physId));

        // Connect spy AFTER refresh — simulates the daemon's
        // connect-onVirtualScreensReconfigured-after-initial-sync pattern.
        QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);

        // Qt signals do not replay past emissions to newly-connected slots,
        // so the spy must see exactly zero signals from the prior refresh.
        // If this assertion ever fails, someone has broken either the
        // signal semantics or has wired Phosphor::Screens::ScreenManager to replay state on
        // connect (e.g. via QMetaObject::invokeMethod from setSettings) —
        // both would re-introduce the boot-time fan-out hazard.
        QCOMPARE(spy.count(), 0);

        // Subsequent changes still fire correctly to the late-connected
        // listener — this confirms the spy is functional and not just
        // missing emissions due to a misconfigured connect.
        mgr.refreshVirtualConfigs({});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), physId);
    }

    /// Regression test for the create→remove→recreate cycle that motivated
    /// the architectural refactor. Simulates the full lifecycle:
    ///   1. Create VS (refreshVirtualConfigs with config)
    ///   2. Remove VS (refreshVirtualConfigs with empty)
    ///   3. Re-create same VS (refreshVirtualConfigs with same config)
    /// Each phase should leave the cache in the expected state and fire
    /// exactly one signal per affected physId.
    void testRefreshVirtualConfigs_createRemoveRecreateCycle()
    {
        Phosphor::Screens::ScreenManager mgr;
        const QString physId = QStringLiteral("LG Electronics:LG Ultra HD:115107");
        const auto config = makeSplitConfig(physId);

        // Phase 1: create
        {
            QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
            QHash<QString, Phosphor::Screens::VirtualScreenConfig> configs{{physId, config}};
            mgr.refreshVirtualConfigs(configs);
            QCOMPARE(spy.count(), 1);
            QVERIFY(mgr.hasVirtualScreens(physId));
            QCOMPARE(mgr.virtualScreenIdsFor(physId).size(), 2);
        }

        // Phase 2: remove (refresh with empty map)
        {
            QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
            mgr.refreshVirtualConfigs({});
            QCOMPARE(spy.count(), 1);
            QCOMPARE(spy.first().first().toString(), physId);
            QVERIFY(!mgr.hasVirtualScreens(physId));
        }

        // Phase 3: re-create with the same config — IDs are deterministic so
        // any prior session/wta state referencing "physId/vs:0" and
        // "physId/vs:1" remains valid after this restoration.
        {
            QSignalSpy spy(&mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);
            QHash<QString, Phosphor::Screens::VirtualScreenConfig> configs{{physId, config}};
            mgr.refreshVirtualConfigs(configs);
            QCOMPARE(spy.count(), 1);
            QVERIFY(mgr.hasVirtualScreens(physId));
            QCOMPARE(mgr.virtualScreenIdsFor(physId),
                     QStringList({
                         PhosphorIdentity::VirtualScreenId::make(physId, 0),
                         PhosphorIdentity::VirtualScreenId::make(physId, 1),
                     }));
        }
    }
};

QTEST_GUILESS_MAIN(TestVirtualScreenManager)
#include "test_virtual_screen_manager.moc"
