// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_virtualscreen.cpp
 * @brief Unit tests for Settings: virtual screen config save/load round-trip
 *
 * Tests cover:
 * 1. Two-screen config save/load round-trip (50/50 split)
 * 2. Three-screen config round-trip (33/33/34 split, float precision)
 * 3. Invalid config rejection on load (region exceeding 1.0)
 * 4. Empty config (removal) round-trip
 * 5. Multiple physical screens save/load independently
 * 6. Signal emission on config change
 */

#include <QTest>
#include <QSignalSpy>

#include "../../../src/config/configdefaults.h"
#include "../../../src/config/configbackends.h"
#include "../../../src/config/settings.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/VirtualScreenTestHelpers.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using PlasmaZones::TestHelpers::makeDef;
using PlasmaZones::TestHelpers::makeSplitConfig;
using PlasmaZones::TestHelpers::makeThreeWayConfig;

class TestSettingsVirtualScreen : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // P0: Two-screen save/load round-trip
    // =========================================================================

    /**
     * Save a 50/50 two-screen config, reload from fresh Settings, verify all
     * fields survive: physicalScreenId, displayName, region (x,y,w,h), index.
     */
    void testSaveLoad_twoScreenConfig()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("Dell:U2722D:115107");

        // Save
        {
            Settings settings;
            settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));
            settings.save();
        }

        // Load into a fresh Settings instance
        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);

        QCOMPARE(loaded.physicalScreenId, physId);
        QCOMPARE(loaded.screens.size(), 2);

        // Left screen
        const Phosphor::Screens::VirtualScreenDef& left = loaded.screens[0];
        QCOMPARE(left.id, PhosphorIdentity::VirtualScreenId::make(physId, 0));
        QCOMPARE(left.physicalScreenId, physId);
        QCOMPARE(left.displayName, QStringLiteral("Left"));
        QCOMPARE(left.index, 0);
        QVERIFY(qAbs(left.region.x()) < 1e-6);
        QVERIFY(qAbs(left.region.y()) < 1e-6);
        QVERIFY(qFuzzyCompare(left.region.width(), 0.5));
        QVERIFY(qFuzzyCompare(left.region.height(), 1.0));

        // Right screen
        const Phosphor::Screens::VirtualScreenDef& right = loaded.screens[1];
        QCOMPARE(right.id, PhosphorIdentity::VirtualScreenId::make(physId, 1));
        QCOMPARE(right.physicalScreenId, physId);
        QCOMPARE(right.displayName, QStringLiteral("Right"));
        QCOMPARE(right.index, 1);
        QVERIFY(qAbs(right.region.x() - 0.5) < 1e-6);
        QVERIFY(qAbs(right.region.y()) < 1e-6);
        QVERIFY(qFuzzyCompare(right.region.width(), 0.5));
        QVERIFY(qFuzzyCompare(right.region.height(), 1.0));
    }

    // =========================================================================
    // P0: Three-screen save/load round-trip (float precision)
    // =========================================================================

    /**
     * 33/33/34 split uses fractional values that test floating-point
     * serialization fidelity (0.333, 0.334, 0.667).
     */
    void testSaveLoad_threeScreenConfig_floatPrecision()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("LG:27GP850:ABC123");

        {
            Settings settings;
            settings.setVirtualScreenConfig(physId, makeThreeWayConfig(physId));
            settings.save();
        }

        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);

        QCOMPARE(loaded.physicalScreenId, physId);
        QCOMPARE(loaded.screens.size(), 3);

        // Verify float precision survives serialization
        // Left: x=0, w=0.333
        QVERIFY(qAbs(loaded.screens[0].region.x() - 0.0) < 1e-6);
        QVERIFY(qAbs(loaded.screens[0].region.width() - 0.333) < 1e-6);
        QCOMPARE(loaded.screens[0].displayName, QStringLiteral("Left"));
        QCOMPARE(loaded.screens[0].index, 0);

        // Center: x=0.333, w=0.334
        QVERIFY(qAbs(loaded.screens[1].region.x() - 0.333) < 1e-6);
        QVERIFY(qAbs(loaded.screens[1].region.width() - 0.334) < 1e-6);
        QCOMPARE(loaded.screens[1].displayName, QStringLiteral("Center"));
        QCOMPARE(loaded.screens[1].index, 1);

        // Right: x=0.667, w=0.333
        QVERIFY(qAbs(loaded.screens[2].region.x() - 0.667) < 1e-6);
        QVERIFY(qAbs(loaded.screens[2].region.width() - 0.333) < 1e-6);
        QCOMPARE(loaded.screens[2].displayName, QStringLiteral("Right"));
        QCOMPARE(loaded.screens[2].index, 2);

        // All regions should have y=0, height=1
        for (int i = 0; i < 3; ++i) {
            QVERIFY(qAbs(loaded.screens[i].region.y() - 0.0) < 1e-6);
            QVERIFY(qAbs(loaded.screens[i].region.height() - 1.0) < 1e-6);
        }
    }

    // =========================================================================
    // P1: Invalid config rejection (region exceeding 1.0)
    // =========================================================================

    /**
     * A config with a region that exceeds 1.0 should be rejected during load.
     * The validator checks x+width <= 1.0+epsilon and y+height <= 1.0+epsilon.
     */
    void testLoad_invalidRegion_rejected()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("test:invalid");

        // Manually write an invalid config to the backend
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto group = backend->group(ConfigDefaults::virtualScreenGroupPrefix() + physId);
            group->writeInt(ConfigDefaults::virtualScreenCountKey(), 2);

            // Screen 0: valid
            group->writeString(QStringLiteral("0/") + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Left"));
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenXKey(), 0.0);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenYKey(), 0.0);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenWidthKey(), 0.5);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenHeightKey(), 1.0);

            // Screen 1: invalid -- x + width = 0.5 + 0.7 = 1.2, exceeds 1.0
            group->writeString(QStringLiteral("1/") + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Right"));
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenXKey(), 0.5);
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenYKey(), 0.0);
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenWidthKey(), 0.7);
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenHeightKey(), 1.0);

            group.reset();
            backend->sync();
        }

        // Load should reject the entire config for this physical screen
        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(loaded.isEmpty(), "Config with region exceeding 1.0 must be rejected on load");
    }

    /**
     * A config with negative region coordinates should be rejected.
     */
    void testLoad_negativeRegion_rejected()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("test:negative");

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto group = backend->group(ConfigDefaults::virtualScreenGroupPrefix() + physId);
            group->writeInt(ConfigDefaults::virtualScreenCountKey(), 1);
            group->writeString(QStringLiteral("0/") + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Bad"));
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenXKey(), -0.1);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenYKey(), 0.0);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenWidthKey(), 0.5);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenHeightKey(), 1.0);
            group.reset();
            backend->sync();
        }

        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(loaded.isEmpty(), "Config with negative region coordinates must be rejected on load");
    }

    /**
     * A config with zero count should be skipped gracefully.
     */
    void testLoad_zeroCount_skipped()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("test:zerocount");

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto group = backend->group(ConfigDefaults::virtualScreenGroupPrefix() + physId);
            group->writeInt(ConfigDefaults::virtualScreenCountKey(), 0);
            group.reset();
            backend->sync();
        }

        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY(loaded.isEmpty());
    }

    // =========================================================================
    // P0: Empty config (removal) round-trip
    // =========================================================================

    /**
     * Setting a config and then replacing it with an empty config (removal)
     * should result in no config for that physical screen after save/load.
     */
    void testSaveLoad_emptyConfig_removesExisting()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("Dell:U2722D:115107");

        // First, save a valid config
        {
            Settings settings;
            settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));
            settings.save();
        }

        // Verify it was saved
        {
            Settings settings;
            QVERIFY(!settings.virtualScreenConfig(physId).isEmpty());
        }

        // Now save an empty config (removal)
        {
            Settings settings;
            Phosphor::Screens::VirtualScreenConfig empty;
            empty.physicalScreenId = physId;
            // screens is empty
            settings.setVirtualScreenConfig(physId, empty);
            settings.save();
        }

        // Verify it was removed
        {
            Settings settings;
            Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
            QVERIFY2(loaded.isEmpty(), "Empty config must remove the virtual screen config on save/load");
        }
    }

    // =========================================================================
    // P1: Multiple physical screens independence
    // =========================================================================

    /**
     * Configs for different physical screens should be saved and loaded
     * independently without interfering with each other.
     */
    void testSaveLoad_multiplePhysicalScreens()
    {
        IsolatedConfigGuard guard;

        const QString physId1 = QStringLiteral("Dell:U2722D:111111");
        const QString physId2 = QStringLiteral("LG:27GP850:222222");

        {
            Settings settings;
            settings.setVirtualScreenConfig(physId1, makeSplitConfig(physId1));
            settings.setVirtualScreenConfig(physId2, makeThreeWayConfig(physId2));
            settings.save();
        }

        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded1 = settings.virtualScreenConfig(physId1);
        Phosphor::Screens::VirtualScreenConfig loaded2 = settings.virtualScreenConfig(physId2);

        QCOMPARE(loaded1.screens.size(), 2);
        QCOMPARE(loaded2.screens.size(), 3);

        // Each config has its own physicalScreenId
        QCOMPARE(loaded1.physicalScreenId, physId1);
        QCOMPARE(loaded2.physicalScreenId, physId2);

        // Screen IDs reference the correct physical screen
        QCOMPARE(loaded1.screens[0].physicalScreenId, physId1);
        QCOMPARE(loaded2.screens[0].physicalScreenId, physId2);
    }

    // =========================================================================
    // P1: Signal emission on config change
    // =========================================================================

    void testSetVirtualScreenConfig_emitsSignal()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QSignalSpy spy(&settings, &Settings::virtualScreenConfigsChanged);
        QVERIFY(spy.isValid());

        const QString physId = QStringLiteral("test:signal");
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));

        QVERIFY2(spy.count() >= 1, "setVirtualScreenConfig must emit virtualScreenConfigsChanged");
    }

    // =========================================================================
    // P1: virtualScreenConfigs() returns all configs
    // =========================================================================

    void testVirtualScreenConfigs_returnsAllConfigs()
    {
        IsolatedConfigGuard guard;

        const QString physId1 = QStringLiteral("screen:one");
        const QString physId2 = QStringLiteral("screen:two");

        Settings settings;
        settings.setVirtualScreenConfig(physId1, makeSplitConfig(physId1));
        settings.setVirtualScreenConfig(physId2, makeThreeWayConfig(physId2));

        QHash<QString, Phosphor::Screens::VirtualScreenConfig> all = settings.virtualScreenConfigs();
        QCOMPARE(all.size(), 2);
        QVERIFY(all.contains(physId1));
        QVERIFY(all.contains(physId2));
        QCOMPARE(all[physId1].screens.size(), 2);
        QCOMPARE(all[physId2].screens.size(), 3);
    }

    // =========================================================================
    // P1: setVirtualScreenConfigs replaces all configs
    // =========================================================================

    void testSetVirtualScreenConfigs_replacesAll()
    {
        IsolatedConfigGuard guard;

        const QString physId1 = QStringLiteral("screen:one");
        const QString physId2 = QStringLiteral("screen:two");

        Settings settings;
        settings.setVirtualScreenConfig(physId1, makeSplitConfig(physId1));
        settings.setVirtualScreenConfig(physId2, makeThreeWayConfig(physId2));

        // Replace with a single config
        QHash<QString, Phosphor::Screens::VirtualScreenConfig> newConfigs;
        newConfigs.insert(physId1, makeThreeWayConfig(physId1));

        settings.setVirtualScreenConfigs(newConfigs);

        QHash<QString, Phosphor::Screens::VirtualScreenConfig> all = settings.virtualScreenConfigs();
        QCOMPARE(all.size(), 1);
        QVERIFY(all.contains(physId1));
        QVERIFY(!all.contains(physId2));
        QCOMPARE(all[physId1].screens.size(), 3);
    }

    // =========================================================================
    // P2: Reset clears virtual screen configs
    // =========================================================================

    void testReset_clearsVirtualScreenConfigs()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("test:reset");

        Settings settings;
        settings.setVirtualScreenConfig(physId, makeSplitConfig(physId));
        settings.save();

        settings.reset();

        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(loaded.isEmpty(), "reset() must clear virtual screen configs");
    }

    // =========================================================================
    // E9: Partial invalidation — 3-screen config with 1 invalid region
    // =========================================================================

    /**
     * Save a 3-screen config where one screen has x > 1.0 (invalid region).
     * On load, the invalid screen is dropped but the two valid screens survive
     * because the loader requires >= 2 valid screens for a meaningful subdivision.
     */
    void testLoad_partialInvalidation_threeScreenConfig()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("test:partial-invalid");

        // Write a 3-screen config directly to the backend: 2 valid + 1 invalid
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto group = backend->group(ConfigDefaults::virtualScreenGroupPrefix() + physId);
            group->writeInt(ConfigDefaults::virtualScreenCountKey(), 3);

            // Screen 0: valid (left half)
            group->writeString(QStringLiteral("0/") + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Left"));
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenXKey(), 0.0);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenYKey(), 0.0);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenWidthKey(), 0.5);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenHeightKey(), 1.0);

            // Screen 1: invalid — x=1.5 exceeds [0,1] bounds
            group->writeString(QStringLiteral("1/") + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Bad"));
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenXKey(), 1.5);
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenYKey(), 0.0);
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenWidthKey(), 0.5);
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenHeightKey(), 1.0);

            // Screen 2: valid (right half)
            group->writeString(QStringLiteral("2/") + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Right"));
            group->writeDouble(QStringLiteral("2/") + ConfigDefaults::virtualScreenXKey(), 0.5);
            group->writeDouble(QStringLiteral("2/") + ConfigDefaults::virtualScreenYKey(), 0.0);
            group->writeDouble(QStringLiteral("2/") + ConfigDefaults::virtualScreenWidthKey(), 0.5);
            group->writeDouble(QStringLiteral("2/") + ConfigDefaults::virtualScreenHeightKey(), 1.0);

            group.reset();
            backend->sync();
        }

        // Load: invalid screen 1 is dropped, 2 valid screens remain (>= 2 threshold met)
        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(!loaded.isEmpty(), "Config with 2 valid screens after invalidation must be kept");
        QCOMPARE(loaded.screens.size(), 2);
        QCOMPARE(loaded.screens[0].displayName, QStringLiteral("Left"));
        QVERIFY(qAbs(loaded.screens[0].region.width() - 0.5) < 1e-6);
        QCOMPARE(loaded.screens[1].displayName, QStringLiteral("Right"));
        QVERIFY(qAbs(loaded.screens[1].region.x() - 0.5) < 1e-6);
        QVERIFY(qAbs(loaded.screens[1].region.width() - 0.5) < 1e-6);
    }

    // =========================================================================
    // E9: Max count boundary — 11 screens capped to 10
    // =========================================================================

    /**
     * Write a config with count=11 to the backend. The loader caps at 10 via
     * qBound(0, count, 10), so only the first 10 screens are read.
     */
    void testLoad_maxCountBoundary()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("test:max-count");

        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto group = backend->group(ConfigDefaults::virtualScreenGroupPrefix() + physId);
            group->writeInt(ConfigDefaults::virtualScreenCountKey(), 11);

            // Write 11 screens: first 10 cover the full width equally,
            // the 11th overlaps and will be dropped by the count cap
            for (int i = 0; i < 11; ++i) {
                const QString p = QString::number(i) + QLatin1Char('/');
                qreal x, w;
                if (i < 10) {
                    w = 1.0 / 10.0;
                    x = i * w;
                } else {
                    // This screen will be dropped by the count cap
                    w = 0.05;
                    x = 0.95;
                }
                group->writeString(p + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Screen %1").arg(i + 1));
                group->writeDouble(p + ConfigDefaults::virtualScreenXKey(), x);
                group->writeDouble(p + ConfigDefaults::virtualScreenYKey(), 0.0);
                group->writeDouble(p + ConfigDefaults::virtualScreenWidthKey(), w);
                group->writeDouble(p + ConfigDefaults::virtualScreenHeightKey(), 1.0);
            }

            group.reset();
            backend->sync();
        }

        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(!loaded.isEmpty(), "Config with capped count must still load");
        QCOMPARE(loaded.screens.size(), 10);
    }

    // =========================================================================
    // P1: Single-screen config is not persisted (requires >= 2)
    // =========================================================================

    /**
     * A config with only one screen is not a meaningful subdivision and must
     * be discarded on save/load. The loader requires >= 2 valid screens.
     */
    void testSaveLoad_singleScreen_discarded()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("EDID-TEST-001");

        {
            Settings settings;
            Phosphor::Screens::VirtualScreenConfig config;
            config.physicalScreenId = physId;
            Phosphor::Screens::VirtualScreenDef def;
            def.id = PhosphorIdentity::VirtualScreenId::make(physId, 0);
            def.physicalScreenId = physId;
            def.index = 0;
            def.region = QRectF(0, 0, 1, 1);
            def.displayName = QStringLiteral("Full");
            config.screens.append(def);
            settings.setVirtualScreenConfig(physId, config);
            settings.save();
        }

        // Reload and verify single-screen config was discarded
        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(loaded.isEmpty(), "Single-screen config must be discarded on load (requires >= 2)");
    }

    // =========================================================================
    // E9: Overlapping regions — rejected (overlap validation IS performed)
    // =========================================================================

    /**
     * Create a 2-screen config with overlapping regions. The loader validates
     * individual region bounds via isValid() AND checks for overlap between
     * regions. Overlapping configs are rejected.
     */
    void testOverlappingRegions()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("test:overlap");

        {
            Settings settings;
            Phosphor::Screens::VirtualScreenConfig config;
            config.physicalScreenId = physId;
            // Screen 0: x=0, w=0.6 — covers [0, 0.6]
            config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.6, 1)));
            // Screen 1: x=0.4, w=0.6 — covers [0.4, 1.0], overlaps with screen 0
            config.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.4, 0, 0.6, 1)));
            settings.setVirtualScreenConfig(physId, config);
            settings.save();
        }

        // Overlapping regions are rejected by the loader
        Settings settings;
        Phosphor::Screens::VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(loaded.isEmpty(), "Overlapping regions must be rejected by the loader");
    }

    /**
     * @brief setVirtualScreenConfig returns false for invalid input.
     *
     * Settings is the single point of admission control for VS configs —
     * the D-Bus adaptor (ScreenAdaptor::setVirtualScreenConfig) defers
     * region/index validation entirely to Settings rather than running its
     * own pre-filter. Pin the contract that:
     *   1. invalid regions return false (so the adaptor can log rejection),
     *   2. valid configs return true,
     *   3. an unchanged config returns true (no-op success).
     */
    void testSetVirtualScreenConfig_returnsFalseOnInvalid()
    {
        IsolatedConfigGuard guard;
        const QString physId = QStringLiteral("test:reject");
        Settings settings;

        // Out-of-bounds region (width > 1.0).
        Phosphor::Screens::VirtualScreenConfig bad;
        bad.physicalScreenId = physId;
        bad.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0.0, 0.0, 1.5, 1.0)));
        bad.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.5, 0.0, 0.5, 1.0)));
        QVERIFY2(!settings.setVirtualScreenConfig(physId, bad),
                 "out-of-bounds region must be rejected by Settings::setVirtualScreenConfig");
        QVERIFY(settings.virtualScreenConfig(physId).isEmpty());

        // Valid config returns true.
        QVERIFY(settings.setVirtualScreenConfig(physId, makeSplitConfig(physId)));

        // No-op: writing the same config again is a successful no-op.
        QVERIFY(settings.setVirtualScreenConfig(physId, makeSplitConfig(physId)));
    }
};

QTEST_MAIN(TestSettingsVirtualScreen)
#include "test_settings_virtualscreen.moc"
