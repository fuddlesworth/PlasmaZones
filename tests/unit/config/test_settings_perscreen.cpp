// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_perscreen.cpp
 * @brief Unit tests for Settings: per-screen config, defaults, edge cases
 *
 * Split from test_settings.cpp. Tests cover:
 * 1. Per-screen zone selector set/clear
 * 2. Per-screen autotile validation
 * 3. Fresh config defaults
 * 4. Empty string list preservation across save/load
 */

#include <QTest>
#include <QSignalSpy>
#include <QVariantMap>

#include "../../../src/config/settings.h"
#include "../../../src/config/configdefaults.h"
#include "../../../src/core/constants.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsPerScreen : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================================
    // Per-screen zone selector
    // =========================================================================

    /**
     * Per-screen zone selector: set an override, verify it resolves, then clear it.
     */
    void testPerScreenZoneSelector_setAndClear()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");

        QVERIFY(!settings.hasPerScreenZoneSelectorSettings(screen));

        settings.setPerScreenZoneSelectorSetting(screen, QStringLiteral("Position"), 3);
        QVERIFY(settings.hasPerScreenZoneSelectorSettings(screen));

        QVariantMap overrides = settings.getPerScreenZoneSelectorSettings(screen);
        QCOMPARE(overrides.value(QStringLiteral("Position")).toInt(), 3);

        // Clear and verify
        settings.clearPerScreenZoneSelectorSettings(screen);
        QVERIFY(!settings.hasPerScreenZoneSelectorSettings(screen));
    }

    // =========================================================================
    // Per-screen autotile validation
    // =========================================================================

    /**
     * Per-screen autotile: validation must reject out-of-range values.
     */
    void testPerScreenAutotile_validationRejectsBadValues()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");

        QSignalSpy spy(&settings, &Settings::perScreenAutotileSettingsChanged);

        // Valid value (long-form key -- normalized to short form internally)
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), 3);
        QCOMPARE(spy.count(), 1);

        // Value is clamped (not rejected outright): 100 should clamp to MaxMasterCount (5)
        settings.setPerScreenAutotileSetting(screen, QStringLiteral("AutotileMasterCount"), 100);
        QVariantMap overrides = settings.getPerScreenAutotileSettings(screen);
        // Key is stored in short form ("MasterCount") after normalization
        int stored = overrides.value(QStringLiteral("MasterCount")).toInt();
        QVERIFY2(stored >= PhosphorTiles::AutotileDefaults::MinMasterCount
                     && stored <= PhosphorTiles::AutotileDefaults::MaxMasterCount,
                 "Per-screen autotile value must be clamped to valid range");
    }

    // =========================================================================
    // P2: edge cases -- fresh config defaults
    // =========================================================================

    /**
     * A fresh config file (no entries) must produce all ConfigDefaults values.
     */
    void testLoad_freshConfig_allDefaults()
    {
        IsolatedConfigGuard guard;

        Settings settings;

        // Activation defaults
        QCOMPARE(settings.zoneSpanEnabled(), ConfigDefaults::zoneSpanEnabled());
        QCOMPARE(settings.toggleActivation(), ConfigDefaults::toggleActivation());
        QCOMPARE(settings.snappingEnabled(), ConfigDefaults::snappingEnabled());

        // Display defaults
        QCOMPARE(settings.showZonesOnAllMonitors(), ConfigDefaults::showOnAllMonitors());
        QCOMPARE(settings.showZoneNumbers(), ConfigDefaults::showNumbers());
        QCOMPARE(settings.flashZonesOnSwitch(), ConfigDefaults::flashOnSwitch());

        // Appearance defaults
        QCOMPARE(settings.borderWidth(), ConfigDefaults::borderWidth());
        QCOMPARE(settings.borderRadius(), ConfigDefaults::borderRadius());
        QCOMPARE(settings.enableBlur(), ConfigDefaults::enableBlur());
        QCOMPARE(settings.labelFontWeight(), ConfigDefaults::labelFontWeight());
        QVERIFY(qFuzzyCompare(settings.labelFontSizeScale(), ConfigDefaults::labelFontSizeScale()));

        // Zone geometry defaults
        QCOMPARE(settings.zonePadding(), ConfigDefaults::zonePadding());
        QCOMPARE(settings.outerGap(), ConfigDefaults::outerGap());
        QCOMPARE(settings.adjacentThreshold(), ConfigDefaults::adjacentThreshold());
        QCOMPARE(settings.pollIntervalMs(), ConfigDefaults::pollIntervalMs());

        // Behavior defaults
        QCOMPARE(settings.keepWindowsInZonesOnResolutionChange(),
                 ConfigDefaults::keepWindowsInZonesOnResolutionChange());
        QCOMPARE(settings.restoreOriginalSizeOnUnsnap(), ConfigDefaults::restoreOriginalSizeOnUnsnap());
        QCOMPARE(settings.excludeTransientWindows(), ConfigDefaults::excludeTransientWindows());

        // Zone selector defaults
        QCOMPARE(settings.zoneSelectorEnabled(), ConfigDefaults::zoneSelectorEnabled());
        QCOMPARE(settings.zoneSelectorTriggerDistance(), ConfigDefaults::triggerDistance());
        QCOMPARE(settings.zoneSelectorGridColumns(), ConfigDefaults::gridColumns());
        QCOMPARE(settings.zoneSelectorMaxRows(), ConfigDefaults::maxRows());

        // Autotile defaults
        QCOMPARE(settings.autotileEnabled(), ConfigDefaults::autotileEnabled());
        QVERIFY(qFuzzyCompare(settings.autotileSplitRatio(), ConfigDefaults::autotileSplitRatio()));
        QCOMPARE(settings.autotileMasterCount(), ConfigDefaults::autotileMasterCount());

        // Animation defaults
        QCOMPARE(settings.animationsEnabled(), ConfigDefaults::animationsEnabled());
        QCOMPARE(settings.animationDuration(), ConfigDefaults::animationDuration());

        // Shader defaults
        QCOMPARE(settings.enableShaderEffects(), ConfigDefaults::enableShaderEffects());
        QCOMPARE(settings.shaderFrameRate(), ConfigDefaults::shaderFrameRate());
    }

    // =========================================================================
    // P2: edge cases -- empty string list preservation
    // =========================================================================

    /**
     * Empty QStringList exclusions must survive a save/load round-trip
     * without becoming null or gaining spurious entries.
     */
    void testSave_load_preservesEmptyStringLists()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        settings.setExcludedApplications(QStringList());
        settings.setExcludedWindowClasses(QStringList());
        settings.save();

        Settings loaded;
        QVERIFY2(loaded.excludedApplications().isEmpty(), "Empty excluded applications list must survive round-trip");
        QVERIFY2(loaded.excludedWindowClasses().isEmpty(),
                 "Empty excluded window classes list must survive round-trip");
    }
};

QTEST_MAIN(TestSettingsPerScreen)
#include "test_settings_perscreen.moc"
