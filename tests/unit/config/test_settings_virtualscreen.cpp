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
#include "../../../src/config/settings.h"
#include "../../../src/core/virtualscreen.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsVirtualScreen : public QObject
{
    Q_OBJECT

private:
    /// Helper: create a VirtualScreenDef with the given parameters
    static VirtualScreenDef makeDef(const QString& physId, int index, const QString& name, const QRectF& region)
    {
        VirtualScreenDef def;
        def.id = VirtualScreenId::make(physId, index);
        def.physicalScreenId = physId;
        def.displayName = name;
        def.region = region;
        def.index = index;
        return def;
    }

    /// Helper: create a two-screen 50/50 split config
    static VirtualScreenConfig makeTwoScreenConfig(const QString& physId)
    {
        VirtualScreenConfig config;
        config.physicalScreenId = physId;
        config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.5, 1)));
        config.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.5, 0, 0.5, 1)));
        return config;
    }

    /// Helper: create a three-screen 33/33/34 split config
    static VirtualScreenConfig makeThreeScreenConfig(const QString& physId)
    {
        VirtualScreenConfig config;
        config.physicalScreenId = physId;
        config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.333, 1)));
        config.screens.append(makeDef(physId, 1, QStringLiteral("Center"), QRectF(0.333, 0, 0.334, 1)));
        config.screens.append(makeDef(physId, 2, QStringLiteral("Right"), QRectF(0.667, 0, 0.333, 1)));
        return config;
    }

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
            settings.setVirtualScreenConfig(physId, makeTwoScreenConfig(physId));
            settings.save();
        }

        // Load into a fresh Settings instance
        Settings settings;
        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);

        QCOMPARE(loaded.physicalScreenId, physId);
        QCOMPARE(loaded.screens.size(), 2);

        // Left screen
        const VirtualScreenDef& left = loaded.screens[0];
        QCOMPARE(left.id, VirtualScreenId::make(physId, 0));
        QCOMPARE(left.physicalScreenId, physId);
        QCOMPARE(left.displayName, QStringLiteral("Left"));
        QCOMPARE(left.index, 0);
        QVERIFY(qFuzzyCompare(left.region.x(), 0.0));
        QVERIFY(qFuzzyCompare(left.region.y(), 0.0));
        QVERIFY(qFuzzyCompare(left.region.width(), 0.5));
        QVERIFY(qFuzzyCompare(left.region.height(), 1.0));

        // Right screen
        const VirtualScreenDef& right = loaded.screens[1];
        QCOMPARE(right.id, VirtualScreenId::make(physId, 1));
        QCOMPARE(right.physicalScreenId, physId);
        QCOMPARE(right.displayName, QStringLiteral("Right"));
        QCOMPARE(right.index, 1);
        QVERIFY(qFuzzyCompare(right.region.x(), 0.5));
        QVERIFY(qFuzzyCompare(right.region.y(), 0.0));
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
            settings.setVirtualScreenConfig(physId, makeThreeScreenConfig(physId));
            settings.save();
        }

        Settings settings;
        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);

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
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
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
        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
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
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
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
        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
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
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
            auto group = backend->group(ConfigDefaults::virtualScreenGroupPrefix() + physId);
            group->writeInt(ConfigDefaults::virtualScreenCountKey(), 0);
            group.reset();
            backend->sync();
        }

        Settings settings;
        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
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
            settings.setVirtualScreenConfig(physId, makeTwoScreenConfig(physId));
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
            VirtualScreenConfig empty;
            empty.physicalScreenId = physId;
            // screens is empty
            settings.setVirtualScreenConfig(physId, empty);
            settings.save();
        }

        // Verify it was removed
        {
            Settings settings;
            VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
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
            settings.setVirtualScreenConfig(physId1, makeTwoScreenConfig(physId1));
            settings.setVirtualScreenConfig(physId2, makeThreeScreenConfig(physId2));
            settings.save();
        }

        Settings settings;
        VirtualScreenConfig loaded1 = settings.virtualScreenConfig(physId1);
        VirtualScreenConfig loaded2 = settings.virtualScreenConfig(physId2);

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
        settings.setVirtualScreenConfig(physId, makeTwoScreenConfig(physId));

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
        settings.setVirtualScreenConfig(physId1, makeTwoScreenConfig(physId1));
        settings.setVirtualScreenConfig(physId2, makeThreeScreenConfig(physId2));

        QHash<QString, VirtualScreenConfig> all = settings.virtualScreenConfigs();
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
        settings.setVirtualScreenConfig(physId1, makeTwoScreenConfig(physId1));
        settings.setVirtualScreenConfig(physId2, makeThreeScreenConfig(physId2));

        // Replace with a single config
        QHash<QString, VirtualScreenConfig> newConfigs;
        newConfigs.insert(physId1, makeThreeScreenConfig(physId1));

        settings.setVirtualScreenConfigs(newConfigs);

        QHash<QString, VirtualScreenConfig> all = settings.virtualScreenConfigs();
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
        settings.setVirtualScreenConfig(physId, makeTwoScreenConfig(physId));
        settings.save();

        settings.reset();

        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
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
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
            auto group = backend->group(ConfigDefaults::virtualScreenGroupPrefix() + physId);
            group->writeInt(ConfigDefaults::virtualScreenCountKey(), 3);

            // Screen 0: valid (left third)
            group->writeString(QStringLiteral("0/") + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Left"));
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenXKey(), 0.0);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenYKey(), 0.0);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenWidthKey(), 0.333);
            group->writeDouble(QStringLiteral("0/") + ConfigDefaults::virtualScreenHeightKey(), 1.0);

            // Screen 1: invalid — x=1.5 exceeds [0,1] bounds
            group->writeString(QStringLiteral("1/") + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Bad"));
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenXKey(), 1.5);
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenYKey(), 0.0);
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenWidthKey(), 0.333);
            group->writeDouble(QStringLiteral("1/") + ConfigDefaults::virtualScreenHeightKey(), 1.0);

            // Screen 2: valid (right third)
            group->writeString(QStringLiteral("2/") + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Right"));
            group->writeDouble(QStringLiteral("2/") + ConfigDefaults::virtualScreenXKey(), 0.667);
            group->writeDouble(QStringLiteral("2/") + ConfigDefaults::virtualScreenYKey(), 0.0);
            group->writeDouble(QStringLiteral("2/") + ConfigDefaults::virtualScreenWidthKey(), 0.333);
            group->writeDouble(QStringLiteral("2/") + ConfigDefaults::virtualScreenHeightKey(), 1.0);

            group.reset();
            backend->sync();
        }

        // Load: invalid screen 1 is dropped, 2 valid screens remain (>= 2 threshold met)
        Settings settings;
        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(!loaded.isEmpty(), "Config with 2 valid screens after invalidation must be kept");
        QCOMPARE(loaded.screens.size(), 2);
        QCOMPARE(loaded.screens[0].displayName, QStringLiteral("Left"));
        QCOMPARE(loaded.screens[1].displayName, QStringLiteral("Right"));
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
            auto backend = PlasmaZones::QSettingsConfigBackend::createDefault();
            auto group = backend->group(ConfigDefaults::virtualScreenGroupPrefix() + physId);
            group->writeInt(ConfigDefaults::virtualScreenCountKey(), 11);

            // Write 11 equal-width screens
            for (int i = 0; i < 11; ++i) {
                const QString p = QString::number(i) + QLatin1Char('/');
                const qreal w = 1.0 / 11.0;
                group->writeString(p + ConfigDefaults::virtualScreenNameKey(), QStringLiteral("Screen %1").arg(i + 1));
                group->writeDouble(p + ConfigDefaults::virtualScreenXKey(), i * w);
                group->writeDouble(p + ConfigDefaults::virtualScreenYKey(), 0.0);
                group->writeDouble(p + ConfigDefaults::virtualScreenWidthKey(), w);
                group->writeDouble(p + ConfigDefaults::virtualScreenHeightKey(), 1.0);
            }

            group.reset();
            backend->sync();
        }

        Settings settings;
        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
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
            VirtualScreenConfig config;
            config.physicalScreenId = physId;
            VirtualScreenDef def;
            def.id = VirtualScreenId::make(physId, 0);
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
        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(loaded.isEmpty(), "Single-screen config must be discarded on load (requires >= 2)");
    }

    // =========================================================================
    // E9: Overlapping regions — accepted (no overlap validation)
    // =========================================================================

    /**
     * Create a 2-screen config with overlapping regions. The loader validates
     * individual region bounds via isValid() but does NOT check for overlap
     * between regions. Both screens should survive the load.
     */
    void testOverlappingRegions()
    {
        IsolatedConfigGuard guard;

        const QString physId = QStringLiteral("test:overlap");

        {
            Settings settings;
            VirtualScreenConfig config;
            config.physicalScreenId = physId;
            // Screen 0: x=0, w=0.6 — covers [0, 0.6]
            config.screens.append(makeDef(physId, 0, QStringLiteral("Left"), QRectF(0, 0, 0.6, 1)));
            // Screen 1: x=0.4, w=0.6 — covers [0.4, 1.0], overlaps with screen 0
            config.screens.append(makeDef(physId, 1, QStringLiteral("Right"), QRectF(0.4, 0, 0.6, 1)));
            settings.setVirtualScreenConfig(physId, config);
            settings.save();
        }

        // Overlapping regions are individually valid, so both survive
        Settings settings;
        VirtualScreenConfig loaded = settings.virtualScreenConfig(physId);
        QVERIFY2(!loaded.isEmpty(), "Overlapping but individually valid regions must be accepted");
        QCOMPARE(loaded.screens.size(), 2);
        QVERIFY(qAbs(loaded.screens[0].region.width() - 0.6) < 1e-6);
        QVERIFY(qAbs(loaded.screens[1].region.x() - 0.4) < 1e-6);
        QVERIFY(qAbs(loaded.screens[1].region.width() - 0.6) < 1e-6);
    }
};

QTEST_MAIN(TestSettingsVirtualScreen)
#include "test_settings_virtualscreen.moc"
