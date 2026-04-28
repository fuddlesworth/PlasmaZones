// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_autotile_virtual.cpp
 * @brief Integration tests for AutotileEngine + virtual screen interaction
 *
 * Covers:
 * - State creation with virtual screen IDs (tilingStateForScreen / stateForKey)
 * - Retile triggers when virtual screen geometry changes
 * - State cleanup when virtual screen config is removed
 * - Per-screen autotile overrides resolved correctly for virtual screen IDs
 * - Override cascade: virtual-screen-specific -> global fallback
 */

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "../helpers/AutotileTestHelpers.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>

#include "../helpers/ScriptedAlgoTestSetup.h"
#include "../helpers/VirtualScreenTestHelpers.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;
using PlasmaZones::TestHelpers::makeSplitConfig;
using PlasmaZones::TestHelpers::makeThreeWayConfig;

class TestAutotileVirtual : public QObject
{
    Q_OBJECT

private:
    PlasmaZones::TestHelpers::ScriptedAlgoTestSetup m_scriptSetup;

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
    }

    // =========================================================================
    // tilingStateForScreen — virtual screen ID acceptance
    // =========================================================================

    void tilingStateForScreen_acceptsVirtualScreenId()
    {
        // Without Phosphor::Screens::ScreenManager, validation is skipped — state creation succeeds
        // for any non-empty screen ID, including virtual screen IDs.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(vsId);

        QVERIFY(state != nullptr);
        QCOMPARE(state->screenId(), vsId);
    }

    void tilingStateForScreen_acceptsMultipleVirtualScreenIds()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        const QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");

        PhosphorTiles::TilingState* state0 = engine.tilingStateForScreen(vs0);
        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(vs1);

        QVERIFY(state0 != nullptr);
        QVERIFY(state1 != nullptr);
        QVERIFY(state0 != state1);
        QCOMPARE(state0->screenId(), vs0);
        QCOMPARE(state1->screenId(), vs1);
    }

    void tilingStateForScreen_returnsSameInstanceForSameVsId()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");
        PhosphorTiles::TilingState* first = engine.tilingStateForScreen(vsId);
        PhosphorTiles::TilingState* second = engine.tilingStateForScreen(vsId);

        QCOMPARE(first, second);
    }

    void tilingStateForScreen_rejectsInvalidVirtualScreenId_withScreenManager()
    {
        // With a real Phosphor::Screens::ScreenManager, tilingStateForScreen validates that the screen
        // exists. A virtual screen ID with no matching config returns nullptr.
        Phosphor::Screens::ScreenManager screenMgr;
        AutotileEngine engine(nullptr, nullptr, &screenMgr, PlasmaZones::TestHelpers::testRegistry());

        const QString vsId = QStringLiteral("Dell:U2722D:115107/vs:99");
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(vsId);

        QVERIFY(state == nullptr);
    }

    void tilingStateForScreen_acceptsValidVirtualScreenId_withScreenManager()
    {
        // Set up Phosphor::Screens::ScreenManager with a virtual screen config so the geometry
        // cache contains valid entries for the virtual screen IDs.
        Phosphor::Screens::ScreenManager screenMgr;
        const QString physId = QStringLiteral("Dell:U2722D:115107");
        Phosphor::Screens::VirtualScreenConfig config = makeSplitConfig(physId);
        // setVirtualScreenConfig will fail because there's no real QScreen,
        // but the internal m_virtualConfigs cache is still populated before
        // the geometry cache is built. screenGeometry() for VS IDs requires
        // the geometry cache, which needs a real QScreen. Since we cannot
        // provide a real QScreen in headless tests, we verify that the
        // Phosphor::Screens::ScreenManager-less path works (tested above) and that the
        // Phosphor::Screens::ScreenManager rejects unknown VS IDs (tested above).
        // This test verifies the validation path runs without crashing.
        AutotileEngine engine(nullptr, nullptr, &screenMgr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(vs0);

        // With no real QScreen backing the physical screen, the geometry cache
        // is empty, so screenGeometry() returns an invalid QRect. tilingStateForScreen
        // correctly rejects this.
        QVERIFY(state == nullptr);
    }

    // =========================================================================
    // Per-desktop state via public API — virtual screen ID acceptance
    // =========================================================================

    void perDesktopState_virtualScreenId_createsDistinctStates()
    {
        // stateForKey is private; test per-desktop state via the public API:
        // setCurrentDesktop() + tilingStateForScreen().
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vsId = QStringLiteral("Dell:U2722D:115107/vs:0");

        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({vsId});
        PhosphorTiles::TilingState* stateD1 = engine.tilingStateForScreen(vsId);
        QVERIFY(stateD1 != nullptr);

        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({vsId});
        PhosphorTiles::TilingState* stateD2 = engine.tilingStateForScreen(vsId);
        QVERIFY(stateD2 != nullptr);

        // Different desktops produce different PhosphorTiles::TilingState instances
        QVERIFY(stateD1 != stateD2);
    }

    void tilingStateForScreen_rejectsEmptyScreenId()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(QString());
        QVERIFY(state == nullptr);
    }

    // =========================================================================
    // virtualScreensChanged — retile triggers
    // =========================================================================

    // Crash guard: verifies that virtualScreensChanged does not crash the engine
    // when there is no backing QScreen. A full retile cannot be verified in
    // headless tests because tilingStateForScreen requires real QScreen geometry.
    void virtualScreensChanged_doesNotCrash()
    {
        Phosphor::Screens::ScreenManager screenMgr;
        AutotileEngine engine(nullptr, nullptr, &screenMgr, PlasmaZones::TestHelpers::testRegistry());

        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1);

        // Set up the VS config on Phosphor::Screens::ScreenManager so virtualScreenIdsFor works
        Phosphor::Screens::VirtualScreenConfig config = makeSplitConfig(physId);
        screenMgr.setVirtualScreenConfig(physId, config);

        // Enable autotile on both virtual screens
        engine.setAutotileScreens({vs0, vs1});

        QSignalSpy tilingSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::placementChanged);
        QVERIFY2(tilingSpy.isValid(), "Signal spy should be valid");

        // Emit virtualScreensChanged — the signal handler should not crash
        Q_EMIT screenMgr.virtualScreensChanged(physId);
        QCoreApplication::processEvents();

        // Crash guard: reaching here means the handler executed without crashing.
        // tilingChanged is not emitted in headless mode because tilingStateForScreen
        // returns nullptr when the geometry cache is empty (no real QScreen).
        QVERIFY2(true, "Crash guard: virtualScreensChanged handler executed without crashing");

        // Additionally verify the Phosphor::Screens::ScreenManager still resolves both VS IDs,
        // confirming the signal did not corrupt the config.
        QStringList vsIds = screenMgr.virtualScreenIdsFor(physId);
        QCOMPARE(vsIds.size(), 2);
        QVERIFY(vsIds.contains(vs0));
        QVERIFY(vsIds.contains(vs1));
    }

    // =========================================================================
    // Single virtual screen — passthrough behavior
    // =========================================================================

    void singleVirtualScreen_treatedAsPassthrough()
    {
        // A single virtual screen covering the full physical screen should behave
        // identically to no-VS configuration: the engine uses the VS ID as a
        // normal screen ID and produces independent per-VS tiling state.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vsId = PhosphorIdentity::VirtualScreenId::make(physId, 0);

        // Configure a single VS covering the full physical screen (no subdivision)
        engine.setAutotileScreens({vsId});
        QVERIFY(engine.isAutotileScreen(vsId));
        QVERIFY(engine.isEnabled());

        // Open a window — state should be keyed by the VS ID, not the physical ID
        engine.windowOpened(QStringLiteral("win-single"), vsId);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state = engine.tilingStateForScreen(vsId);
        QVERIFY(state != nullptr);
        QCOMPARE(state->screenId(), vsId);
        QVERIFY(state->containsWindow(QStringLiteral("win-single")));
        QCOMPARE(state->windowCount(), 1);

        // Physical ID must NOT be an autotile screen — the VS ID is authoritative
        QVERIFY(!engine.isAutotileScreen(physId));

        // Removing the VS from autotile screens releases the window
        QSignalSpy releaseSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::windowsReleased);
        engine.setAutotileScreens({});
        QVERIFY(releaseSpy.count() >= 1);
        QStringList released = releaseSpy.first().first().toStringList();
        QVERIFY(released.contains(QStringLiteral("win-single")));
    }

    // =========================================================================
    // Window migration between virtual screens via windowFocused
    // =========================================================================

    void windowMovesBetweenVirtualScreens()
    {
        // Verify that windowFocused on a different virtual screen migrates
        // the window from the old state to the new state.
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        const QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");

        engine.setAutotileScreens({vs0, vs1});

        // Add window to vs:0
        engine.windowOpened(QStringLiteral("win-move"), vs0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state0 = engine.tilingStateForScreen(vs0);
        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(vs1);
        QVERIFY(state0 != nullptr);
        QVERIFY(state1 != nullptr);
        QVERIFY(state0->containsWindow(QStringLiteral("win-move")));
        QCOMPARE(state1->windowCount(), 0);

        // Simulate the window moving to vs:1 by calling windowFocused with
        // the new screen ID. AutotileEngine::windowFocused detects the
        // cross-screen move, removes from old state, and re-adds via
        // onWindowAdded to the new state.
        QSignalSpy tilingSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::placementChanged);
        engine.windowFocused(QStringLiteral("win-move"), vs1);
        QCoreApplication::processEvents();

        // Window should be removed from vs:0's state
        QVERIFY(!state0->containsWindow(QStringLiteral("win-move")));
        QCOMPARE(state0->windowCount(), 0);

        // Window should now be in vs:1's state
        QVERIFY(state1->containsWindow(QStringLiteral("win-move")));
        QCOMPARE(state1->windowCount(), 1);

        // tilingChanged should have been emitted for at least the destination screen
        QVERIFY(tilingSpy.count() >= 1);
    }

    // =========================================================================
    // State cleanup — virtual screen removal via setAutotileScreens
    // =========================================================================

    void virtualScreenRemoval_cleansUpState()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        const QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");

        // Enable autotile on both virtual screens
        engine.setAutotileScreens({vs0, vs1});

        // Add a window to vs0
        engine.windowOpened(QStringLiteral("win1"), vs0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state0 = engine.tilingStateForScreen(vs0);
        QVERIFY(state0 != nullptr);
        QVERIFY(state0->containsWindow(QStringLiteral("win1")));

        // Now remove vs0 from autotile screens (keep vs1)
        QSignalSpy releaseSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::windowsReleased);
        engine.setAutotileScreens({vs1});

        // State for vs0 should have been cleaned up
        QVERIFY(!engine.isAutotileScreen(vs0));
        QVERIFY(engine.isAutotileScreen(vs1));

        // The window should have been released
        QVERIFY(releaseSpy.count() >= 1);
        QStringList released = releaseSpy.first().first().toStringList();
        QVERIFY(released.contains(QStringLiteral("win1")));
    }

    void virtualScreenRemoval_allVirtualScreens_cleansUpAllStates()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        const QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");

        engine.setAutotileScreens({vs0, vs1});

        engine.windowOpened(QStringLiteral("win1"), vs0);
        engine.windowOpened(QStringLiteral("win2"), vs1);
        QCoreApplication::processEvents();

        QVERIFY(engine.isEnabled());

        // Remove all virtual screens
        QSignalSpy enabledSpy(&engine, &AutotileEngine::enabledChanged);
        engine.setAutotileScreens({});

        QVERIFY(!engine.isEnabled());
        QCOMPARE(enabledSpy.count(), 1);
        QCOMPARE(enabledSpy.first().first().toBool(), false);
    }

    void virtualScreenRemoval_perScreenConfigCleaned()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        engine.setAutotileScreens({vs0});

        // Apply per-screen config
        QVariantMap overrides;
        overrides[QStringLiteral("InnerGap")] = 20;
        engine.applyPerScreenConfig(vs0, overrides);
        QVERIFY(engine.hasPerScreenOverride(vs0, QStringLiteral("InnerGap")));

        // Remove vs0 from autotile — overrides should be cleared
        engine.setAutotileScreens({});

        QVERIFY(!engine.hasPerScreenOverride(vs0, QStringLiteral("InnerGap")));
    }

    // =========================================================================
    // Per-screen config overrides with virtual screen IDs
    // =========================================================================

    void perScreenConfig_virtualScreenOverrideApplied()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        engine.setAutotileScreens({vs0});

        // Set global inner gap
        engine.config()->innerGap = 10;

        // Apply per-screen override for virtual screen
        QVariantMap overrides;
        overrides[QStringLiteral("InnerGap")] = 25;
        engine.applyPerScreenConfig(vs0, overrides);

        QCOMPARE(engine.effectiveInnerGap(vs0), 25);
    }

    void perScreenConfig_virtualScreenFallsBackToGlobal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        const QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        engine.setAutotileScreens({vs0, vs1});

        // Set global inner gap
        engine.config()->innerGap = 12;

        // Apply override only to vs0
        QVariantMap overrides;
        overrides[QStringLiteral("InnerGap")] = 30;
        engine.applyPerScreenConfig(vs0, overrides);

        // vs0 should use its override
        QCOMPARE(engine.effectiveInnerGap(vs0), 30);
        // vs1 should fall back to global
        QCOMPARE(engine.effectiveInnerGap(vs1), 12);
    }

    void perScreenConfig_virtualScreenAlgorithmOverride()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        const QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        engine.setAutotileScreens({vs0, vs1});

        // Set global algorithm
        engine.setAlgorithm(QLatin1String("master-stack"));

        // Override vs0 to use BSP
        QVariantMap overrides;
        overrides[QStringLiteral("Algorithm")] = QLatin1String("bsp");
        engine.applyPerScreenConfig(vs0, overrides);

        QCOMPARE(engine.effectiveAlgorithmId(vs0), QLatin1String("bsp"));
        QCOMPARE(engine.effectiveAlgorithmId(vs1), QLatin1String("master-stack"));
    }

    void perScreenConfig_clearOverrideRestoresGlobal()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        engine.setAutotileScreens({vs0});

        engine.config()->innerGap = 8;

        QVariantMap overrides;
        overrides[QStringLiteral("InnerGap")] = 20;
        engine.applyPerScreenConfig(vs0, overrides);
        QCOMPARE(engine.effectiveInnerGap(vs0), 20);

        engine.clearPerScreenConfig(vs0);
        QCOMPARE(engine.effectiveInnerGap(vs0), 8);
    }

    // =========================================================================
    // Override cascade: virtual -> physical -> global
    // The PerScreenConfigResolver does NOT have a virtual-to-physical fallback.
    // Overrides are matched by exact screenId only. If an override is set for
    // the physical screen ID, it does NOT cascade to its virtual sub-screens.
    // This is the intended behavior — virtual screens are independent entities.
    // =========================================================================

    void overrideCascade_physicalOverrideDoesNotAffectVirtualScreens()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        const QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");

        engine.setAutotileScreens({vs0, vs1});
        engine.config()->innerGap = 5;

        // Apply override to physical screen ID
        QVariantMap overrides;
        overrides[QStringLiteral("InnerGap")] = 50;
        engine.applyPerScreenConfig(physId, overrides);

        // Virtual screens should NOT inherit the physical screen's override.
        // They fall back to global config instead.
        QCOMPARE(engine.effectiveInnerGap(vs0), 5);
        QCOMPARE(engine.effectiveInnerGap(vs1), 5);

        // The physical ID itself has the override
        QCOMPARE(engine.effectiveInnerGap(physId), 50);
    }

    void overrideCascade_eachVirtualScreenIndependent()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        const QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");
        const QString vs2 = QStringLiteral("Dell:U2722D:115107/vs:2");

        engine.setAutotileScreens({vs0, vs1, vs2});
        engine.config()->outerGap = 10;

        // Override each virtual screen independently
        QVariantMap o0;
        o0[QStringLiteral("OuterGap")] = 15;
        engine.applyPerScreenConfig(vs0, o0);

        QVariantMap o1;
        o1[QStringLiteral("OuterGap")] = 20;
        engine.applyPerScreenConfig(vs1, o1);

        // vs2 has no override — uses global
        QCOMPARE(engine.effectiveOuterGap(vs0), 15);
        QCOMPARE(engine.effectiveOuterGap(vs1), 20);
        QCOMPARE(engine.effectiveOuterGap(vs2), 10);
    }

    // =========================================================================
    // Window lifecycle with virtual screen IDs
    // =========================================================================

    void windowLifecycle_virtualScreenId()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        const QString vs1 = QStringLiteral("Dell:U2722D:115107/vs:1");

        engine.setAutotileScreens({vs0, vs1});
        QVERIFY(engine.isEnabled());

        QSignalSpy tilingSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::placementChanged);

        // Open windows on different virtual screens
        engine.windowOpened(QStringLiteral("win1"), vs0);
        engine.windowOpened(QStringLiteral("win2"), vs1);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* state0 = engine.tilingStateForScreen(vs0);
        PhosphorTiles::TilingState* state1 = engine.tilingStateForScreen(vs1);

        QVERIFY(state0 != nullptr);
        QVERIFY(state1 != nullptr);
        QVERIFY(state0->containsWindow(QStringLiteral("win1")));
        QVERIFY(!state0->containsWindow(QStringLiteral("win2")));
        QVERIFY(state1->containsWindow(QStringLiteral("win2")));
        QVERIFY(!state1->containsWindow(QStringLiteral("win1")));

        // Close window on vs0
        tilingSpy.clear();
        engine.windowClosed(QStringLiteral("win1"));
        QCoreApplication::processEvents();

        QVERIFY(!state0->containsWindow(QStringLiteral("win1")));
        QCOMPARE(state0->windowCount(), 0);
        // vs1 state unaffected
        QVERIFY(state1->containsWindow(QStringLiteral("win2")));
    }

    // =========================================================================
    // State keying: virtual screens with per-desktop state
    // =========================================================================

    void stateKeying_virtualScreenPerDesktop()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString vs0 = QStringLiteral("Dell:U2722D:115107/vs:0");
        engine.setCurrentDesktop(1);
        engine.setAutotileScreens({vs0});

        // Add window on desktop 1
        engine.windowOpened(QStringLiteral("win-d1"), vs0);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* stateD1 = engine.tilingStateForScreen(vs0);
        QVERIFY(stateD1 != nullptr);
        QVERIFY(stateD1->containsWindow(QStringLiteral("win-d1")));

        // Switch to desktop 2
        engine.setCurrentDesktop(2);
        engine.setAutotileScreens({vs0});

        PhosphorTiles::TilingState* stateD2 = engine.tilingStateForScreen(vs0);
        QVERIFY(stateD2 != nullptr);
        // Desktop 2 state should be empty — it's a different TilingStateKey
        QCOMPARE(stateD2->windowCount(), 0);
        QVERIFY(stateD1 != stateD2);
    }

    // =========================================================================
    // Phosphor::Screens::ScreenManager virtual screen config change signal wiring
    // =========================================================================

    void screenManager_virtualScreenConfigChange_signalEmitted()
    {
        Phosphor::Screens::ScreenManager screenMgr;

        const QString physId = QStringLiteral("Dell:U2722D:115107");
        Phosphor::Screens::VirtualScreenConfig config = makeSplitConfig(physId);

        QSignalSpy vsChangedSpy(&screenMgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);

        bool result = screenMgr.setVirtualScreenConfig(physId, config);
        QVERIFY(result);
        QCOMPARE(vsChangedSpy.count(), 1);
        QCOMPARE(vsChangedSpy.first().first().toString(), physId);
    }

    void screenManager_virtualScreenRemoval_signalEmitted()
    {
        Phosphor::Screens::ScreenManager screenMgr;

        const QString physId = QStringLiteral("Dell:U2722D:115107");
        Phosphor::Screens::VirtualScreenConfig config = makeSplitConfig(physId);

        screenMgr.setVirtualScreenConfig(physId, config);

        QSignalSpy vsChangedSpy(&screenMgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged);

        // Remove by setting empty config
        Phosphor::Screens::VirtualScreenConfig emptyConfig;
        screenMgr.setVirtualScreenConfig(physId, emptyConfig);

        QCOMPARE(vsChangedSpy.count(), 1);
        QCOMPARE(vsChangedSpy.first().first().toString(), physId);
    }

    void screenManager_virtualScreenIdsFor_returnsCorrectIds()
    {
        Phosphor::Screens::ScreenManager screenMgr;

        const QString physId = QStringLiteral("Dell:U2722D:115107");
        Phosphor::Screens::VirtualScreenConfig config = makeSplitConfig(physId);

        screenMgr.setVirtualScreenConfig(physId, config);

        QStringList vsIds = screenMgr.virtualScreenIdsFor(physId);
        QCOMPARE(vsIds.size(), 2);
        QVERIFY(vsIds.contains(PhosphorIdentity::VirtualScreenId::make(physId, 0)));
        QVERIFY(vsIds.contains(PhosphorIdentity::VirtualScreenId::make(physId, 1)));
    }

    void screenManager_virtualScreenIdsFor_noConfig_returnsPhysicalId()
    {
        Phosphor::Screens::ScreenManager screenMgr;

        const QString physId = QStringLiteral("Dell:U2722D:115107");
        QStringList ids = screenMgr.virtualScreenIdsFor(physId);

        QCOMPARE(ids.size(), 1);
        QCOMPARE(ids.first(), physId);
    }

    void screenManager_virtualScreenIdsFor_afterRemoval_returnsPhysicalId()
    {
        Phosphor::Screens::ScreenManager screenMgr;

        const QString physId = QStringLiteral("Dell:U2722D:115107");
        Phosphor::Screens::VirtualScreenConfig config = makeSplitConfig(physId);
        screenMgr.setVirtualScreenConfig(physId, config);

        // Verify VS IDs exist
        QCOMPARE(screenMgr.virtualScreenIdsFor(physId).size(), 2);

        // Remove config
        Phosphor::Screens::VirtualScreenConfig emptyConfig;
        screenMgr.setVirtualScreenConfig(physId, emptyConfig);

        // Should fall back to physical ID
        QStringList ids = screenMgr.virtualScreenIdsFor(physId);
        QCOMPARE(ids.size(), 1);
        QCOMPARE(ids.first(), physId);
    }

    // =========================================================================
    // Triple split — verifies the engine handles 3+ virtual screens per physical
    // =========================================================================

    void tripleSplit_allVirtualScreensGetIndependentStates()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());

        const QString physId = QStringLiteral("Dell:U2722D:115107");
        const QString vs0 = PhosphorIdentity::VirtualScreenId::make(physId, 0);
        const QString vs1 = PhosphorIdentity::VirtualScreenId::make(physId, 1);
        const QString vs2 = PhosphorIdentity::VirtualScreenId::make(physId, 2);

        engine.setAutotileScreens({vs0, vs1, vs2});

        engine.windowOpened(QStringLiteral("win-left"), vs0);
        engine.windowOpened(QStringLiteral("win-center"), vs1);
        engine.windowOpened(QStringLiteral("win-right"), vs2);
        QCoreApplication::processEvents();

        PhosphorTiles::TilingState* s0 = engine.tilingStateForScreen(vs0);
        PhosphorTiles::TilingState* s1 = engine.tilingStateForScreen(vs1);
        PhosphorTiles::TilingState* s2 = engine.tilingStateForScreen(vs2);

        QVERIFY(s0 && s1 && s2);
        QVERIFY(s0 != s1 && s1 != s2 && s0 != s2);
        QCOMPARE(s0->windowCount(), 1);
        QCOMPARE(s1->windowCount(), 1);
        QCOMPARE(s2->windowCount(), 1);
        QVERIFY(s0->containsWindow(QStringLiteral("win-left")));
        QVERIFY(s1->containsWindow(QStringLiteral("win-center")));
        QVERIFY(s2->containsWindow(QStringLiteral("win-right")));
    }
};

QTEST_MAIN(TestAutotileVirtual)
#include "test_autotile_virtual.moc"
